/*
 * GLRenderTarget.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "GLRenderTarget.h"
#include "../RenderState/GLStateManager.h"
#include "../Ext/GLExtensions.h"
#include "../../GLCommon/GLExtensionRegistry.h"
#include "../../CheckedCast.h"
#include "../../GLCommon/GLTypes.h"
#include "../../GLCommon/GLCore.h"
#include "../../../Core/Helper.h"


namespace LLGL
{


/*
 * Internals
 */

static const std::size_t g_maxFramebufferAttachments = 32;

[[noreturn]]
static void ErrDepthAttachmentFailed()
{
    throw std::runtime_error("attachment to render target failed, because render target already has a depth-stencil buffer");
}

[[noreturn]]
static void ErrTooManyColorAttachments(std::size_t numColorAttchments)
{
    throw std::runtime_error(
        "too many color attachments for render target (" +
        std::to_string(numColorAttchments) + " is specified, but limit is " +  std::to_string(g_maxFramebufferAttachments) + ")"
    );
}

static void ValidateFramebufferStatus(const char* info)
{
    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GLThrowIfFailed(status, GL_FRAMEBUFFER_COMPLETE, info);
}

static std::size_t CountColorAttachments(const std::vector<AttachmentDescriptor>& attachmentDescs)
{
    std::size_t numColorAttachments = 0;

    for (const auto& attachmentDesc : attachmentDescs)
    {
        if (attachmentDesc.type == AttachmentType::Color)
            ++numColorAttachments;
    }

    if (numColorAttachments > g_maxFramebufferAttachments)
        ErrTooManyColorAttachments(g_maxFramebufferAttachments);

    return numColorAttachments;
}


/*
 * GLRenderTarget class
 */

GLRenderTarget::GLRenderTarget(const RenderTargetDescriptor& desc) :
    RenderTarget  { desc.resolution                                        },
    multiSamples_ { static_cast<GLsizei>(desc.multiSampling.SampleCount()) }
{
    framebuffer_.GenFramebuffer();
    if (desc.attachments.empty())
        CreateFramebufferWithNoAttachments(desc);
    else
        CreateFramebufferWithAttachments(desc);
}

std::uint32_t GLRenderTarget::GetNumColorAttachments() const
{
    return static_cast<std::uint32_t>(colorAttachments_.size());
}

bool GLRenderTarget::HasDepthAttachment() const
{
    return ((blitMask_ & GL_DEPTH_BUFFER_BIT) != 0);
}

bool GLRenderTarget::HasStencilAttachment() const
{
    return ((blitMask_ & GL_STENCIL_BUFFER_BIT) != 0);
}

/* ----- Extended Internal Functions ----- */

//private
void GLRenderTarget::BlitFramebuffer()
{
    GLFramebuffer::Blit(
        static_cast<GLint>(GetResolution().width),
        static_cast<GLint>(GetResolution().height),
        blitMask_
    );
}

/*
Blit (or rather copy) each multi-sample attachment from the
multi-sample framebuffer (read) into the main framebuffer (draw)
*/
void GLRenderTarget::BlitOntoFramebuffer()
{
    if (framebufferMS_ && !colorAttachments_.empty())
    {
        framebuffer_.Bind(GLFramebufferTarget::DRAW_FRAMEBUFFER);
        framebufferMS_.Bind(GLFramebufferTarget::READ_FRAMEBUFFER);

        for (auto attachment : colorAttachments_)
        {
            glReadBuffer(attachment);
            glDrawBuffer(attachment);
            BlitFramebuffer();
        }

        framebufferMS_.Unbind(GLFramebufferTarget::READ_FRAMEBUFFER);
        framebuffer_.Unbind(GLFramebufferTarget::DRAW_FRAMEBUFFER);
    }
}

/*
Blit (or rather copy) each multi-sample attachment from the
multi-sample framebuffer (read) into the back buffer (draw)
*/
void GLRenderTarget::BlitOntoScreen(std::size_t colorAttachmentIndex)
{
    if (colorAttachmentIndex < colorAttachments_.size())
    {
        GLStateManager::active->BindFramebuffer(GLFramebufferTarget::DRAW_FRAMEBUFFER, 0);
        GLStateManager::active->BindFramebuffer(GLFramebufferTarget::READ_FRAMEBUFFER, GetFramebuffer().GetID());
        {
            glReadBuffer(colorAttachments_[colorAttachmentIndex]);
            glDrawBuffer(GL_BACK);
            BlitFramebuffer();
        }
        GLStateManager::active->BindFramebuffer(GLFramebufferTarget::READ_FRAMEBUFFER, 0);
    }
}

const GLFramebuffer& GLRenderTarget::GetFramebuffer() const
{
    return (framebufferMS_.Valid() ? framebufferMS_ : framebuffer_);
}


/*
 * ======= Private: =======
 */

void GLRenderTarget::CreateFramebufferWithAttachments(const RenderTargetDescriptor& desc)
{
    /* Create secondary FBO if standard multi-sampling is enabled */
    if (HasMultiSampling() && !desc.customMultiSampling)
        framebufferMS_.GenFramebuffer();

    /* Determine number of color attachments */
    GLenum internalFormats[g_maxFramebufferAttachments];
    auto numColorAttachments = CountColorAttachments(desc.attachments);

    /* Reserve storage for color attachment slots */
    colorAttachments_.reserve(numColorAttachments);

    /* Bind primary FBO */
    GLStateManager::active->BindFramebuffer(GLFramebufferTarget::FRAMEBUFFER, framebuffer_.GetID());
    {
        if (framebufferMS_)
        {
            /* Only attach textures (renderbuffers are only attached to multi-sampled FBO) */
            AttachAllTextures(desc.attachments, internalFormats);
        }
        else
        {
            /* Attach all depth-stencil buffers and textures if multi-sampling is disabled */
            AttachAllDepthStencilBuffers(desc.attachments);
            AttachAllTextures(desc.attachments, internalFormats);
            SetDrawBuffers();
        }

        /* Validate framebuffer status */
        ValidateFramebufferStatus("color attachment to framebuffer object (FBO) failed");
    }

    /* Create renderbuffers for multi-sampled render-target */
    if (framebufferMS_)
    {
        /* Bind multi-sampled FBO */
        GLStateManager::active->BindFramebuffer(GLFramebufferTarget::FRAMEBUFFER, framebufferMS_.GetID());
        {
            /* Create depth-stencil attachmnets */
            AttachAllDepthStencilBuffers(desc.attachments);

            /* Create all renderbuffers as storage source for multi-sampled render target */
            CreateRenderbuffersMS(internalFormats);
        }
    }
}

void GLRenderTarget::CreateFramebufferWithNoAttachments(const RenderTargetDescriptor& desc)
{
    #ifdef GL_ARB_framebuffer_no_attachments
    if (HasExtension(GLExt::ARB_framebuffer_no_attachments))
    {
        /* Set default framebuffer parameters */
        framebuffer_.FramebufferParameters(
            static_cast<GLint>(desc.resolution.width),
            static_cast<GLint>(desc.resolution.height),
            1,
            static_cast<GLint>(multiSamples_),
            0
        );
    }
    else
    #endif // /GL_ARB_framebuffer_no_attachments
    {
        /* Bind primary FBO */
        GLStateManager::active->BindFramebuffer(GLFramebufferTarget::FRAMEBUFFER, framebuffer_.GetID());

        /* Create dummy renderbuffer attachment */
        renderbuffer_.GenRenderbuffer();
        renderbuffer_.Storage(
            GL_RED,
            static_cast<GLsizei>(desc.resolution.width),
            static_cast<GLsizei>(desc.resolution.height),
            multiSamples_
        );

        /* Attach dummy renderbuffer to first color attachment slot */
        framebuffer_.AttachRenderbuffer(GL_COLOR_ATTACHMENT0, renderbuffer_.GetID());
    }

    /* Validate framebuffer status */
    ValidateFramebufferStatus("initializing default parameters for framebuffer object (FBO) failed");
}

void GLRenderTarget::AttachAllTextures(const std::vector<AttachmentDescriptor>& attachmentDescs, GLenum* internalFormats)
{
    std::size_t colorAttachmentIndex = 0;

    for (const auto& attachmentDesc : attachmentDescs)
    {
        if (auto texture = attachmentDesc.texture)
        {
            /* Attach texture as color attachment */
            AttachTexture(*texture, attachmentDesc, internalFormats[colorAttachmentIndex++]);
        }
    }
}

void GLRenderTarget::AttachAllDepthStencilBuffers(const std::vector<AttachmentDescriptor>& attachmentDescs)
{
    for (const auto& attachmentDesc : attachmentDescs)
    {
        if (attachmentDesc.texture == nullptr)
        {
            /* Attach (and create) depth-stencil buffer */
            switch (attachmentDesc.type)
            {
                case AttachmentType::Color:
                    throw std::invalid_argument("cannot have color attachment in render target without a valid texture");
                    break;
                case AttachmentType::Depth:
                    AttachDepthBuffer();
                    break;
                case AttachmentType::DepthStencil:
                    AttachDepthStencilBuffer();
                    break;
                case AttachmentType::Stencil:
                    AttachStencilBuffer();
                    break;
            }
        }
    }
}

void GLRenderTarget::AttachDepthBuffer()
{
    CreateAndAttachRenderbuffer(GL_DEPTH_COMPONENT, GL_DEPTH_ATTACHMENT);
    blitMask_ |= (GL_DEPTH_BUFFER_BIT);
}

void GLRenderTarget::AttachStencilBuffer()
{
    CreateAndAttachRenderbuffer(GL_STENCIL_INDEX, GL_STENCIL_ATTACHMENT);
    blitMask_ |= (GL_STENCIL_BUFFER_BIT);
}

void GLRenderTarget::AttachDepthStencilBuffer()
{
    CreateAndAttachRenderbuffer(GL_DEPTH_STENCIL, GL_DEPTH_STENCIL_ATTACHMENT);
    blitMask_ |= (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

// Returns the GL internal format for the specified texture object
static GLint GetTexInternalFormat(const GLTexture& textureGL)
{
    GLint internalFormat = GL_RGBA;
    {
        GLStateManager::active->BindTexture(textureGL);
        glGetTexLevelParameteriv(GLTypes::Map(textureGL.GetType()), 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    }
    return internalFormat;
}

void GLRenderTarget::AttachTexture(Texture& texture, const AttachmentDescriptor& attachmentDesc, GLenum& internalFormat)
{
    /* Get OpenGL texture object */
    auto& textureGL = LLGL_CAST(GLTexture&, texture);
    auto textureID = textureGL.GetID();

    /* Validate resolution for MIP-map level */
    auto mipLevel = attachmentDesc.mipLevel;
    ValidateMipResolution(texture, mipLevel);

    /* Make color or depth-stencil attachment */
    internalFormat = static_cast<GLenum>(GetTexInternalFormat(textureGL));
    auto attachment = MakeFramebufferAttachment(internalFormat);

    /* Attach texture to framebuffer */
    switch (texture.GetType())
    {
        case TextureType::Texture1D:
            GLFramebuffer::AttachTexture1D(attachment, GL_TEXTURE_1D, textureID, static_cast<GLint>(mipLevel));
            break;
        case TextureType::Texture2D:
            GLFramebuffer::AttachTexture2D(attachment, GL_TEXTURE_2D, textureID, static_cast<GLint>(mipLevel));
            break;
        case TextureType::Texture3D:
            GLFramebuffer::AttachTexture3D(attachment, GL_TEXTURE_3D, textureID, static_cast<GLint>(mipLevel), static_cast<GLint>(attachmentDesc.arrayLayer));
            break;
        case TextureType::TextureCube:
            GLFramebuffer::AttachTexture2D(attachment, GLTypes::ToTextureCubeMap(attachmentDesc.arrayLayer), textureID, static_cast<GLint>(mipLevel));
            break;
        case TextureType::Texture1DArray:
        case TextureType::Texture2DArray:
        case TextureType::TextureCubeArray:
            GLFramebuffer::AttachTextureLayer(attachment, textureID, static_cast<GLint>(mipLevel), static_cast<GLint>(attachmentDesc.arrayLayer));
            break;
        case TextureType::Texture2DMS:
            GLFramebuffer::AttachTexture2D(attachment, GL_TEXTURE_2D_MULTISAMPLE, textureID, 0);
            break;
        case TextureType::Texture2DMSArray:
            GLFramebuffer::AttachTextureLayer(attachment, textureID, 0, static_cast<GLint>(attachmentDesc.arrayLayer));
            break;
    }
}

void GLRenderTarget::CreateRenderbuffersMS(const GLenum* internalFormats)
{
    /* Create renderbuffer for attachment if multi-sample framebuffer is used */
    renderbuffersMS_.reserve(colorAttachments_.size());

    /* Create alll renderbuffers as storage for multi-sampled attachments */
    for (std::size_t i = 0; i < colorAttachments_.size(); ++i)
        CreateRenderbufferMS(colorAttachments_[i], internalFormats[i]);

    /* Set draw buffers for this framebuffer is multi-sampling is enabled */
    SetDrawBuffers();

    /* Validate framebuffer status */
    ValidateFramebufferStatus("color attachments to multi-sample framebuffer object (FBO) failed");
}

void GLRenderTarget::CreateRenderbufferMS(GLenum attachment, GLenum internalFormat)
{
    GLRenderbuffer renderbuffer;
    renderbuffer.GenRenderbuffer();
    {
        /* Setup renderbuffer storage by texture's internal format */
        InitRenderbufferStorage(renderbuffer, internalFormat);

        /* Attach renderbuffer to multi-sample framebuffer */
        GLFramebuffer::AttachRenderbuffer(attachment, renderbuffer.GetID());
    }
    renderbuffersMS_.emplace_back(std::move(renderbuffer));
}

void GLRenderTarget::InitRenderbufferStorage(GLRenderbuffer& renderbuffer, GLenum internalFormat)
{
    renderbuffer.Storage(
        internalFormat,
        static_cast<GLsizei>(GetResolution().width),
        static_cast<GLsizei>(GetResolution().height),
        multiSamples_
    );
}

void GLRenderTarget::CreateAndAttachRenderbuffer(GLenum internalFormat, GLenum attachment)
{
    if (!renderbuffer_)
    {
        /* Create renderbuffer for depth-stencil attachment */
        renderbuffer_.GenRenderbuffer();

        /* Setup renderbuffer storage */
        InitRenderbufferStorage(renderbuffer_, internalFormat);

        /* Attach renderbuffer to framebuffer (or multi-sample framebuffer if multi-sampling is used) */
        GLFramebuffer::AttachRenderbuffer(attachment, renderbuffer_.GetID());
    }
    else
        ErrDepthAttachmentFailed();
}

GLenum GLRenderTarget::MakeFramebufferAttachment(GLenum internalFormat)
{
    if (internalFormat == GL_DEPTH_COMPONENT)
    {
        if (!HasDepthStencilAttachment())
        {
            /* Add depth attachment and depth buffer bit to blit mask */
            blitMask_ |= GL_DEPTH_BUFFER_BIT;
            return GL_DEPTH_ATTACHMENT;
        }
        else
            ErrDepthAttachmentFailed();
    }
    else if (internalFormat == GL_DEPTH_STENCIL)
    {
        if (!HasDepthStencilAttachment())
        {
            /* Add depth-stencil attachment and depth-stencil buffer bit to blit mask */
            blitMask_ |= (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            return GL_DEPTH_STENCIL_ATTACHMENT;
        }
        else
            ErrDepthAttachmentFailed();
    }
    else
    {
        /* Add color attachment and color buffer bit to blit mask */
        blitMask_ |= GL_COLOR_BUFFER_BIT;
        const GLenum attachment = (GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(colorAttachments_.size()));
        colorAttachments_.push_back(attachment);
        return attachment;
    }
}

void GLRenderTarget::SetDrawBuffers()
{
    /*
    Tell OpenGL which buffers are to be written when drawing operations are performed.
    Each color attachment has its own draw buffer.
    */
    if (colorAttachments_.empty())
        glDrawBuffer(GL_NONE);
    else if (colorAttachments_.size() == 1)
        glDrawBuffer(colorAttachments_.front());
    else
        glDrawBuffers(static_cast<GLsizei>(colorAttachments_.size()), colorAttachments_.data());
}

bool GLRenderTarget::HasMultiSampling() const
{
    return (multiSamples_ > 1);
}

bool GLRenderTarget::HasCustomMultiSampling() const
{
    return (framebufferMS_.Valid());
}

bool GLRenderTarget::HasDepthStencilAttachment() const
{
    static const GLbitfield mask = (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    return ((blitMask_ & mask) != 0);
}


} // /namespace LLGL



// ================================================================================
