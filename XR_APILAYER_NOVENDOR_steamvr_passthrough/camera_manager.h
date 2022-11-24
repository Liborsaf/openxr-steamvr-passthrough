#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <xr_linear.h>
#include "passthrough_renderer.h"


enum ETrackedCameraFrameType
{
	VRFrameType_Distorted = 0,
	VRFrameType_Undistorted,
	VRFrameType_MaximumUndistorted
};

#define POSTFRAME_SLEEP_INTERVAL (std::chrono::milliseconds(10))
#define FRAME_POLL_INTERVAL (std::chrono::microseconds(100))


class CameraManager
{
public:

	CameraManager(std::shared_ptr<IPassthroughRenderer> renderer, std::shared_ptr<ConfigManager> configManager);
	~CameraManager();

	bool InitRuntime();
	void DeinitRuntime();

	bool InitCamera();
	void DeinitCamera();

	void GetFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize);
	void UpdateStaticCameraParameters();
	bool GetCameraFrame(std::shared_ptr<CameraFrame>& frame);
	void CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrTime& displayTime, const XrReferenceSpaceCreateInfo& refSpaceInfo);

private:
	void ServeFrames();
	void GetTrackedCameraEyePoses(XrMatrix4x4f& LeftPose, XrMatrix4x4f& RightPose);
	XrMatrix4x4f GetHMDMVPMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo);
	void CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo);

	std::shared_ptr<ConfigManager> m_configManager;

	bool m_bRuntimeInitialized = false;
	bool m_bCameraInitialized = false;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	float m_projectionDistanceFar;
	float m_projectionDistanceNear;

	std::weak_ptr<IPassthroughRenderer> m_renderer;
	std::thread m_serveThread;
	std::atomic_bool m_bRunThread = true;
	std::mutex m_serveMutex;

	std::shared_ptr<CameraFrame> m_renderFrame;
	std::shared_ptr<CameraFrame> m_servedFrame;

	vr::IVRTrackedCamera* m_trackedCamera;
	int m_hmdDeviceId = -1;
	vr::EVRTrackedCameraFrameType m_frameType;
	vr::TrackedCameraHandle_t m_cameraHandle;
	EStereoFrameLayout m_frameLayout;

	XrMatrix4x4f m_rawHMDProjectionLeft{};
	XrMatrix4x4f m_rawHMDViewLeft{};
	XrMatrix4x4f m_rawHMDProjectionRight{};
	XrMatrix4x4f m_rawHMDViewRight{};
	XrMatrix4x4f m_cameraLeftToHMDPose{};
	XrMatrix4x4f m_cameraLeftToRightPose{};
};

