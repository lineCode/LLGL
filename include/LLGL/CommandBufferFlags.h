/*
 * CommandBufferFlags.h
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef LLGL_COMMAND_BUFFER_FLAGS_H
#define LLGL_COMMAND_BUFFER_FLAGS_H


#include "ColorRGBA.h"


namespace LLGL
{


/* ----- Enumerations ----- */

/**
\brief Render condition mode enumeration.
\remarks The condition is determined by the type of the Query object.
\see RenderContext::BeginRenderCondition
*/
enum class RenderConditionMode
{
    Wait,                   //!< Wait until the occlusion query result is available, before conditional rendering begins.
    NoWait,                 //!< Do not wait until the occlusion query result is available, before conditional rendering begins.
    ByRegionWait,           //!< Similar to Wait, but the renderer may discard the results of commands for any framebuffer region that did not contribute to the occlusion query.
    ByRegionNoWait,         //!< Similar to NoWait, but the renderer may discard the results of commands for any framebuffer region that did not contribute to the occlusion query.
    WaitInverted,           //!< Same as Wait, but the condition is inverted.
    NoWaitInverted,         //!< Same as NoWait, but the condition is inverted.
    ByRegionWaitInverted,   //!< Same as ByRegionWait, but the condition is inverted.
    ByRegionNoWaitInverted, //!< Same as ByRegionNoWait, but the condition is inverted.
};


/* ----- Structures ----- */

/**
\brief Command buffer clear flags.
\see CommandBuffer::Clear
*/
struct ClearFlags
{
    enum
    {
        Color           = (1 << 0),                     //!< Clears the color attachment.
        Depth           = (1 << 1),                     //!< Clears the depth attachment.
        Stencil         = (1 << 2),                     //!< Clears the stencil attachment.

        ColorDepth      = (Color | Depth),              //!< Clears the color and depth attachments.
        DepthStencil    = (Depth | Stencil),            //!< Clears the depth and stencil attachments.
        All             = (Color | Depth | Stencil),    //!< Clears the color, depth and stencil attachments.
    };
};

/**
\brief Clear value structure for color, depth, and stencil clear operations.
\see AttachmentClear::clearValue
\see ImageInitialization::clearValue
*/
struct ClearValue
{
    //! Specifies the clear value to clear a color attachment. By default (0.0, 0.0, 0.0, 0.0).
    ColorRGBAf      color   = { 0.0f, 0.0f, 0.0f, 0.0f };

    //! Specifies the clear value to clear a depth attachment. By default 1.0.
    float           depth   = 1.0f;

    //! Specifies the clear value to clear a stencil attachment. By default 0.
    std::uint32_t   stencil = 0;
};

/**
\brief Attachment clear command structure.
\see CommandBuffer::ClearAttachments
*/
struct AttachmentClear
{
    AttachmentClear() = default;
    AttachmentClear(const AttachmentClear&) = default;
    AttachmentClear& operator = (const AttachmentClear&) = default;

    //! Constructor for a color attachment clear command.
    inline AttachmentClear(const ColorRGBAf& color, std::uint32_t colorAttachment) :
        flags           { ClearFlags::Color },
        colorAttachment { colorAttachment   }
    {
        clearValue.color = color;
    }

    //! Constructor for a depth attachment clear command.
    inline AttachmentClear(float depth) :
        flags { ClearFlags::Depth }
    {
        clearValue.depth = depth;
    }

    //! Constructor for a stencil attachment clear command.
    inline AttachmentClear(std::uint32_t stencil) :
        flags { ClearFlags::Stencil }
    {
        clearValue.stencil = stencil;
    }

    //! Constructor for a depth-stencil attachment clear command.
    inline AttachmentClear(float depth, std::uint32_t stencil) :
        flags { ClearFlags::DepthStencil }
    {
        clearValue.depth    = depth;
        clearValue.stencil  = stencil;
    }

    /**
    \brief Specifies the clear buffer flags.
    \remarks This can be a bitwise OR combination of the "ClearFlags" enumeration entries.
    However, if the ClearFlags::Color bit is set, all other bits are ignored.
    It is recommended to clear depth- and stencil buffers always simultaneously if both are meant to be cleared (i.e. use ClearFlags::DepthStencil in this case).
    \see ClearFlags
    */
    long            flags           = 0;

    /**
    \brief Specifies the index of the color attachment within the active render target. By default 0.
    \remarks This is ignored if the ClearFlags::Color bit is not set in the 'flags' member.
    \see flags
    */
    std::uint32_t   colorAttachment = 0;

    //! Clear value for color, depth, and stencil buffers.
    ClearValue      clearValue;
};

/**
\brief Graphics API dependent state descriptor for the OpenGL renderer.
\remarks This descriptor is used to compensate a few differences between OpenGL and the other rendering APIs.
\see RenderContext::SetGraphicsAPIDependentState
*/
struct OpenGLDependentStateDescriptor
{
    /**
    \brief Specifies whether the screen-space origin is on the lower-left. By default false.
    \remarks If this is true, the viewports and scissor rectangles of OpenGL are NOT emulated to the upper-left,
    which is the default to be uniform with other rendering APIs such as Direct3D and Vulkan.
    */
    bool originLowerLeft = false;

    /**
    \brief Specifies whether to invert front-facing. By default false.
    \remarks If this is true, the front facing (either GL_CW or GL_CCW) will be inverted,
    i.e. CCW becomes CW, and CW becomes CCW.
    */
    bool invertFrontFace = false;
};


} // /namespace LLGL


#endif



// ================================================================================
