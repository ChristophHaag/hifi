//
//  OsvrDisplayPlugin.cpp
//  plugins/hifiOSVR/src
//
//  Created by David Rowe on 5 Jul 2016.
//  Copyright 2016 High Fidelity, Inc.
//
//  Assumes OSVR RenderManager is configured with asynchronous timewarp enabled.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "OsvrDisplayPlugin.h"

#include <ViewFrustum.h>
#include <display-plugins/CompositorHelper.h>

#include "OsvrHelpers.h"

// The following need to come after other #includes.
#include <GL/gl.h>
#include <osvr/RenderKit/GraphicsLibraryOpenGL.h>  // Must be included after <GL/gl.h>


const QString OsvrDisplayPlugin::NAME("OSVR HMD");

bool OsvrDisplayPlugin::isSupported() const {
    return isOsvrDisplayAvailable();
}

void OsvrDisplayPlugin::init() {
    // Assumes isSupported() is true.
    Plugin::init();
    emit deviceConnected(getName());
}

bool OsvrDisplayPlugin::isHmdMounted() const {
    // Is the user wearing the HMD?

    // OSVR doesn't currently have any function reflecting this. Instead, just return whether the HMD plug-in is activated.
    return _isActivated;
}

float OsvrDisplayPlugin::getTargetFrameRate() const {
    // HMD display refresh rate.
    return _targetFrameRate;
}

void OsvrDisplayPlugin::resetSensors() {
    // Reset HMD tracking.

    if (_osvrContext && _osvrRender) {
        withRenderThreadLock([&] {
            _osvrContext->update();  // Update tracker state.
            _sensorZeroRotation = glm::inverse(toGlm(_osvrRender->GetRenderInfo()[0].pose.rotation));
            _sensorZeroTranslation = -0.5f * (toGlm(_osvrRender->GetRenderInfo()[0].pose.translation) 
                + toGlm(_osvrRender->GetRenderInfo()[1].pose.translation));
        });
    }
}

void OsvrDisplayPlugin::cycleDebugOutput() {
    // HMD-specific debug output.

    // OSVR doesn't have any specific debug overlay or similar; just lock the current texture instead.
    _lockCurrentTexture = !_lockCurrentTexture;
}

bool OsvrDisplayPlugin::internalActivate() {
    // Set up HMD rendering.

    qDebug() << "OSVR: Activate";

    // Initialize OSVR rendering
    _osvrContext = getOsvrContext();
    _osvrRender = osvr::renderkit::createRenderManager(_osvrContext->get(), "OpenGL");
    if ((_osvrRender == nullptr) || (!_osvrRender->doingOkay())) {
        qWarning() << "OSVR: Could not create RenderManager";
        if (_osvrRender != nullptr) {
            delete _osvrRender;
            _osvrRender = nullptr;
        }
        return false;
    }
    _osvrContext->update();

    auto hmdInfo = parseHMDInfo(_osvrContext->getStringParameter("/display"));
    qDebug() << "OSVR: HMD =" << hmdInfo[HMD_VENDOR] << hmdInfo[HMD_MODEL] << hmdInfo[HMD_VERSION];

    osvr::renderkit::RenderManager::OpenResults result = _osvrRender->OpenDisplay();
    if (result.status == osvr::renderkit::RenderManager::OpenStatus::FAILURE) {
        qWarning() << "OSVR: Could not open display";
        delete _osvrRender;
        _osvrRender = nullptr;
        return false;
    }
    if (result.library.OpenGL == nullptr) {
        qWarning() << "OSVR: Graphics library not configured as OpenGL";
        delete _osvrRender;
        _osvrRender = nullptr;
        return false;
    }

    if (_osvrRender->GetRenderInfo(_renderParams).size() != 2) {
        qWarning() << "OSVR: Display does not have 2 eyes";
        delete _osvrRender;
        _osvrRender = nullptr;
        return false;
    }

    // Get HMD's target frame rate.
    osvr::renderkit::RenderTimingInfo timingInfo;
    if (_osvrRender->GetTimingInfo(0, timingInfo)) {
        const float MICROSECONDS_PER_SECOND = 1000000.0f;
        _targetFrameRate = roundf(MICROSECONDS_PER_SECOND / (float)timingInfo.hardwareDisplayInterval.microseconds);
        qDebug() << "OSVR: HMD frame rate =" << _targetFrameRate;
    } else {
        const float DEFAULT_TARGET_FRAME_RATE = 60.0f;
        _targetFrameRate = DEFAULT_TARGET_FRAME_RATE;
        qDebug() << "OSVR: Could not obtain HMD's frame rate; using" << _targetFrameRate;
    }

    // Get HMD's IPD.
    osvr::clientkit::DisplayConfig* display = getOsvrDisplay();
    osvr::clientkit::Eye eyes[2] = { display->getEye(0, 0), display->getEye(0, 1) };
    OSVR_Pose3 eyePoses[2];
    if (eyes[0].getPose(eyePoses[0]) && eyes[1].getPose(eyePoses[1])) {
        _ipd = glm::distance(toGlm(eyePoses[0].translation), toGlm(eyePoses[1].translation));
        qDebug() << "OSVR: IPD =" << _ipd;
    } else {
        qWarning() << "OSVR: Could not obtain HMD's IPD; using " << _ipd;
    }

    // Set up Interface render parameters.
    std::vector<osvr::renderkit::RenderInfo> renderInfo;
    _renderParams.nearClipDistanceMeters = DEFAULT_NEAR_CLIP;
    _renderParams.farClipDistanceMeters = DEFAULT_FAR_CLIP;
    _renderParams.IPDMeters = _ipd;
    renderInfo = _osvrRender->GetRenderInfo(_renderParams);
    if (renderInfo.size() != 2) {  // Check again to be sure; size() has been observed to occasionally be 0 during operation.
        qWarning() << "OSVR: Could not obtain render info";
        delete _osvrRender;
        _osvrRender = nullptr;
        return false;
    }

    fixRenderInfo(renderInfo);  // TODO: Is this correct when using an OSVR HDK?

    _renderTargetSize = uvec2(
        renderInfo[0].viewport.width + renderInfo[1].viewport.width,
        std::max(renderInfo[0].viewport.height, renderInfo[1].viewport.height)
        );

    _eyeProjections[0] = toGlm(renderInfo[0].projection);
    _eyeProjections[1] = toGlm(renderInfo[1].projection);
    _eyeOffsets[0] = glm::translate(mat4(), glm::vec3(-_ipd / 2.0f, 0.0f, 0.0f));
    _eyeOffsets[1] = glm::translate(mat4(), glm::vec3(_ipd / 2.0f, 0.0f, 0.0f));

    _cullingProjection = _eyeProjections[0];  // Eyes have identical projections.
    if (_eyeProjections[0] != _eyeProjections[1]) {
        // Warn if not the case in the future.
        qWarning() << "OSVR: Improper culling projection because eye projections are not identical";
    }

    // Set up OSVR render parameters.
    osvr::renderkit::OSVR_ViewportDescription textureViewportLeft{ 0.0f, 0.0f, 0.5f, 1.0f };
    _textureViewports.push_back(textureViewportLeft);
    osvr::renderkit::OSVR_ViewportDescription textureViewportRight{ 0.5f, 0.0f, 0.5f, 1.0f };
    _textureViewports.push_back(textureViewportRight);

    _isActivated = Parent::internalActivate();
    if (!_isActivated) {
        delete _osvrRender;
        _osvrRender = nullptr;
    }

    return _isActivated;
}

void OsvrDisplayPlugin::customizeContext() {
    // Customize OpenGL context.

    // Display plug-in DLLs must initialize glew locally.
    static std::once_flag once;
    std::call_once(once, [] {
        glewExperimental = true;
        GLenum err = glewInit();
        glGetError();  // Clear the potential error from glewExperimental.
    });

    Parent::customizeContext();  // Must be before RegisterRenderBuffers().

    // Make OSVR display from Interface frame buffer.
    _colorBuffer.OpenGL = new osvr::renderkit::RenderBufferOpenGL;
    _colorBuffer.OpenGL->colorBufferName = GetName(_compositeFramebuffer->color);
    _colorBuffers.clear();
    _colorBuffers.push_back(_colorBuffer);
    _colorBuffers.push_back(_colorBuffer);  // Single buffer for both eyes.
    if (!_osvrRender->RegisterRenderBuffers(_colorBuffers)) {
        qWarning() << "OSVR: Could not register render buffers";
    }
}

bool OsvrDisplayPlugin::beginFrameRender(uint32_t frameIndex) {
    // Prepare to render frame.

    _osvrContext->update();  // Update tracker state.
    _renderInfo = _osvrRender->GetRenderInfo(_renderParams);
    if (_renderInfo.size() != 2) {
        qWarning() << "OSVR: No eye information available to render frame";
        return false;
    }

    fixRenderInfo(_renderInfo);  // TODO: Is this correct when using an OSVR HDK?

    _currentRenderFrameInfo = FrameInfo();
    glm::quat rotation = toGlm(_renderInfo[0].pose.rotation) * _sensorZeroRotation;  // Both eye views have the same rotation.
    glm::vec3 translation = rotation 
        * ((toGlm(_renderInfo[0].pose.translation) + toGlm(_renderInfo[1].pose.translation)) / 2.0f + _sensorZeroTranslation);

    _currentRenderFrameInfo.renderPose = glm::translate(glm::mat4(), translation) * glm::mat4_cast(rotation);
    _currentRenderFrameInfo.presentPose = _currentRenderFrameInfo.renderPose;

    withRenderThreadLock([&] {
        _uiModelTransform = DependencyManager::get<CompositorHelper>()->getModelTransform();
        _frameInfos[frameIndex] = _currentRenderFrameInfo;
    });

    return Parent::beginFrameRender(frameIndex);
}

void OsvrDisplayPlugin::hmdPresent() {
    // Submit frame to HMD.

    if (_renderedFrame == _presentedFrame) {
        return;  // Only submit new frame so that OSVR RenderManager's asynchronous timewarp can do its thing.
    }
    _presentedFrame = _renderedFrame;

    const bool FLIP_IN_Y = true;

    if (!_osvrRender->PresentRenderBuffers(_colorBuffers, _presentInfo, _renderParams, _textureViewports, FLIP_IN_Y)) {
        qWarning() << "OSVR: Failed to present image on HMD";
    }
};

void OsvrDisplayPlugin::submitSceneTexture(uint32_t frameIndex, const gpu::TexturePointer& sceneTexture) {
    // Identify newly rendered frame for hmdPresent().

    _renderedFrame = frameIndex;
    _presentInfo = _renderInfo;

    Parent::submitSceneTexture(frameIndex, sceneTexture);
}

void OsvrDisplayPlugin::uncustomizeContext() {
    // Revert OpenGL context to desktop's.
    _colorBuffers.clear();
    delete _colorBuffer.OpenGL;
    _colorBuffer.OpenGL = nullptr;

    Parent::uncustomizeContext();
}

void OsvrDisplayPlugin::internalDeactivate() {
    // Tear down HMD rendering.

    qDebug() << "OSVR: Deactivate";

    Parent::internalDeactivate();
    delete _osvrRender;
    _osvrRender = nullptr;

    _isActivated = false;
}
