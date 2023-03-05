#include "pch.h"
#include "dashboard_menu.h"
#include <log.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"

#include "fonts/roboto_medium.cpp"

using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


DashboardMenu::DashboardMenu(HMODULE dllModule, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
	: m_dllModule(dllModule)
	, m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_thumbnailHandle(vr::k_ulOverlayHandleInvalid)
	, m_bMenuIsVisible(false)
	, m_displayValues()
	, m_activeTab(TabMain)
{
	m_bRunThread = true;
	m_menuThread = std::thread(&DashboardMenu::RunThread, this);
}


DashboardMenu::~DashboardMenu()
{
	m_bRunThread = false;
	if (m_menuThread.joinable())
	{
		m_menuThread.join();
	}
}


void DashboardMenu::RunThread()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	while (!vrOverlay)
	{
		if (!m_bRunThread)
		{
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		vrOverlay = m_openVRManager->GetVROverlay();
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(OVERLAY_RES_WIDTH, OVERLAY_RES_HEIGHT);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	m_mainFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 24);
	m_smallFont = io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, 22);
	ImGui::StyleColorsDark();

	SetupDX11();
	ImGui_ImplDX11_Init(m_d3d11Device.Get(), m_d3d11DeviceContext.Get());
	CreateOverlay();


	while (m_bRunThread)
	{
		TickMenu();

		vrOverlay->WaitFrameSync(100);
	}


	ImGui_ImplDX11_Shutdown();
	ImGui::GetIO().BackendRendererUserData = NULL;
	ImGui::DestroyContext();

	if (vrOverlay)
	{
		vrOverlay->DestroyOverlay(m_overlayHandle);
	}
}


std::string GetImageFormatName(ERenderAPI api, int64_t format)
{
	switch (api)
	{
	case DirectX11:
	case DirectX12:

		switch (format)
		{		
		case 29:
			return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
		case 91:
			return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
		case 2:
			return "DXGI_FORMAT_R32G32B32A32_FLOAT";
		case 10:
			return "DXGI_FORMAT_R16G16B16A16_FLOAT";
		case 24:
			return "DXGI_FORMAT_R10G10B10A2_UNORM";
		case 40:
			return "DXGI_FORMAT_D32_FLOAT";
		case 55:
			return "DXGI_FORMAT_D16_UNORM";
		case 45:
			return "DXGI_FORMAT_D24_UNORM_S8_UINT";
		case 20:
			return "DXGI_FORMAT_D32_FLOAT_S8X24_UINT";

		default:
			return "Unknown format";
		}
	case Vulkan:

		switch (format)
		{
		case 43:
			return "VK_FORMAT_R8G8B8A8_SRGB";
		case 50:
			return "VK_FORMAT_B8G8R8A8_SRGB";
		case 109: 
			return "VK_FORMAT_R32G32B32A32_SFLOAT";
		case 106: 
			return "VK_FORMAT_R32G32B32_SFLOAT";
		case 97: 
			return "VK_FORMAT_R16G16B16A16_SFLOAT";
		case 126: 
			return "VK_FORMAT_D32_SFLOAT";
		case 124: 
			return "VK_FORMAT_D16_UNORM";
		case 129: 
			return "VK_FORMAT_D24_UNORM_S8_UINT";
		case 130:
			return "VK_FORMAT_D32_SFLOAT_S8_UINT";

		default:
			return "Unknown format";
		}
		
	default:
		return "Unknown format";
	}
}


void ScrollableSlider(const char* label, float* v, float v_min, float v_max, const char* format, float scrollFactor)
{
	ImGui::SliderFloat(label, v, v_min, v_max, format, ImGuiSliderFlags_None);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	if (ImGui::IsItemHovered())
	{
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel)
		{
			if (ImGui::IsItemActive())
			{
				ImGui::ClearActiveID();
			}
			else
			{
				*v += wheel * scrollFactor;
				if (*v < v_min) { *v = v_min; }
				else if (*v > v_max) { *v = v_max; }
			}
		}
	}
}


void ScrollableSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, int scrollFactor)
{
	ImGui::SliderInt(label, v, v_min, v_max, format, ImGuiSliderFlags_None);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	if (ImGui::IsItemHovered())
	{
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel)
		{
			if (ImGui::IsItemActive())
			{
				ImGui::ClearActiveID();
			}
			else
			{
				*v += (int)wheel * scrollFactor;
				if (*v < v_min) { *v = v_min; }
				else if (*v > v_max) { *v = v_max; }
			}
		}
	}
}

inline void DashboardMenu::TextDescription(const char* fmt, ...)
{
	ImGui::Indent();
	ImGui::PushFont(m_smallFont);
	ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
	va_list args;
	va_start(args, fmt);
	ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), fmt, args);
	va_end(args);
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	ImGui::Unindent();
}

#define IMGUI_BIG_SPACING ImGui::Dummy(ImVec2(0.0f, 20.0f))

void DashboardMenu::TickMenu() 
{
	HandleEvents();

	if (!m_bMenuIsVisible)
	{
		return;
	}

	Config_Main& mainConfig = m_configManager->GetConfig_Main();
	Config_Core& coreConfig = m_configManager->GetConfig_Core();
	Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();
	Config_Stereo& stereoCustomConfig = m_configManager->GetConfig_CustomStereo();

	ImVec4 colorTextGreen(0.2f, 0.8f, 0.2f, 1.0f);
	ImVec4 colorTextRed(0.8f, 0.2f, 0.2f, 1.0f);

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplDX11_NewFrame();

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 20);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 20);
	ImGui::PushFont(m_mainFont);

	ImGui::Begin("OpenXR Passthrough", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

	

	ImGui::BeginChild("Tab buttons", ImVec2(OVERLAY_RES_WIDTH * 0.18f, 0));

	ImVec2 tabButtonSize(OVERLAY_RES_WIDTH * 0.17f, 55);
	ImVec4 colorActiveTab(0.25f, 0.52f, 0.88f, 1.0f);
	bool bIsActiveTab = false;

#define TAB_BUTTON(name, tab) if (m_activeTab == tab) { ImGui::PushStyleColor(ImGuiCol_Button, colorActiveTab); bIsActiveTab = true; } \
if (ImGui::Button(name, tabButtonSize)) { m_activeTab = tab; } \
if (bIsActiveTab) { ImGui::PopStyleColor(1); bIsActiveTab = false; }

	TAB_BUTTON("Main", TabMain);
	TAB_BUTTON("Application", TabApplication);
	TAB_BUTTON("Stereo", TabStereo);
	TAB_BUTTON("Overrides", TabOverrides);
	TAB_BUTTON("Debug", TabDebug);


	ImGui::BeginChild("Sep1", ImVec2(0, 70));
	ImGui::EndChild();

	ImGui::Indent();
	ImGui::Text("Application:");
	ImGui::Text("%s", m_displayValues.currentApplication.c_str());

	ImGui::Separator();
	ImGui::Text("Session:");
	m_displayValues.bSessionActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");

	ImGui::Separator();
	ImGui::Text("Passthrough:");
	m_displayValues.bCorePassthroughActive ? ImGui::TextColored(colorTextGreen, "Active") : ImGui::TextColored(colorTextRed, "Inactive");
	ImGui::Unindent();

	ImGui::BeginChild("Sep2", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 30));
	ImGui::EndChild();
	if (ImGui::Button("Reset To Defaults", tabButtonSize))
	{
		m_configManager->ResetToDefaults();
	}

	ImGui::EndChild();
	ImGui::SameLine();




	if (m_activeTab == TabMain)
	{
		ImGui::BeginChild("Main#TabMain");


		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Main Settings"))
		{
			ImGui::Checkbox("Enable Passthrough", &mainConfig.EnablePassthrough);

			IMGUI_BIG_SPACING;

			ImGui::Text("Projection Mode");
			TextDescription("Method for projectiong the passthrough cameras to the VR view.");
			if (ImGui::RadioButton("Room View 2D", mainConfig.ProjectionMode == ProjectionRoomView2D))
			{
				mainConfig.ProjectionMode = ProjectionRoomView2D;
			}
			TextDescription("Cylindrical projection with floor. Matches the projection in the SteamVR Room View 2D mode.");

			if (ImGui::RadioButton("Custom 2D (Experimental)", mainConfig.ProjectionMode == ProjectionCustom2D))
			{
				mainConfig.ProjectionMode = ProjectionCustom2D;
			}
			TextDescription("Cylindrical projection with floor. Custom distortion correction and projection calculation.");

			if (ImGui::RadioButton("Stereo 3D (Experimental)", mainConfig.ProjectionMode == ProjectionStereoReconstruction))
			{
				mainConfig.ProjectionMode = ProjectionStereoReconstruction;
			}
			TextDescription("Accurate depth estimation.");
			IMGUI_BIG_SPACING;

			ScrollableSlider("Opacity", &mainConfig.PassthroughOpacity, 0.0f, 1.0f, "%.1f", 0.1f);
			ScrollableSlider("Brightness", &mainConfig.Brightness, -50.0f, 50.0f, "%.0f", 1.0f);
			ScrollableSlider("Contrast", &mainConfig.Contrast, 0.0f, 2.0f, "%.1f", 0.1f);
			ScrollableSlider("Saturation", &mainConfig.Saturation, 0.0f, 2.0f, "%.1f", 0.1f);
			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Projection Settings"))
		{
			ImGui::Spacing();
			ScrollableSlider("Projection Distance (m)", &mainConfig.ProjectionDistanceFar, 0.5f, 20.0f, "%.1f", 0.1f);
			TextDescription("The horizontal projection distance in 2D modes, and maximum projection distance in the 3D mode.");

			ScrollableSlider("Floor Height Offset (m)", &mainConfig.FloorHeightOffset, 0.0f, 2.0f, "%.2f", 0.01f);
			TextDescription("Allows setting the floor height higher in the 2D modes, for example to have correct projection on a table surface.");

			ScrollableSlider("Field of View Scale", &mainConfig.FieldOfViewScale, 0.0f, 1.0f, "%.1f", 0.0f);
			TextDescription("Sets the size of the rendered area in the Custom 2D and Stereo 3D projection modes.");
		}	
		IMGUI_BIG_SPACING;

		ImGui::EndChild();
	}



	if(m_activeTab == TabApplication)
	{
		ImGui::BeginChild("Setup#Tabsetup");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("OpenXR Core"))
		{
			TextDescription("Options for application controlled passthrough features built into the OpenXR core specification. Allows using the environment blend modes for passthrough.");
			IMGUI_BIG_SPACING;

			ImGui::Text("Core passthrough:");
			ImGui::SameLine();
			if (m_displayValues.bCorePassthroughActive)
			{
				ImGui::TextColored(colorTextGreen, "Active");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Inactive");
			}

			ImGui::Text("Application requested mode:");
			ImGui::SameLine();
			if (m_displayValues.CoreCurrentMode == 3) { ImGui::Text("Alpha Blend"); }
			else if (m_displayValues.CoreCurrentMode == 2) { ImGui::Text("Additive"); }
			else if (m_displayValues.CoreCurrentMode == 1) { ImGui::Text("Opaque"); }
			else { ImGui::Text("Unknown"); }

			IMGUI_BIG_SPACING;

			ImGui::Checkbox("Enable###CoreEnable", &coreConfig.CorePassthroughEnable);
			TextDescription("Allow OpenXR applications to enable passthrough.");

			IMGUI_BIG_SPACING;

			ImGui::BeginGroup();
			ImGui::Text("Blend Modes");
			TextDescription("Controls what blend modes are presented to the application. Requires a restart to apply.");
			ImGui::Checkbox("Alpha Blend###CoreAlpha", &coreConfig.CoreAlphaBlend);
			ImGui::Checkbox("Additive###CoreAdditive", &coreConfig.CoreAdditive);
			ImGui::EndGroup();

			IMGUI_BIG_SPACING;

			ImGui::BeginGroup();
			ImGui::Text("Preferred Mode");
			TextDescription("Sets which blend mode the application should prefer. Most game engines will always use the preferred mode by default, even if the application does not support passthrough. Requires a restart to apply.");
			if (ImGui::RadioButton("Alpha Blend###CorePref3", coreConfig.CorePreferredMode == 3))
			{
				coreConfig.CorePreferredMode = 3;
			}
			if (ImGui::RadioButton("Additive###CorePref2", coreConfig.CorePreferredMode == 2))
			{
				coreConfig.CorePreferredMode = 2;
			}
			if (ImGui::RadioButton("Opaque###CorePref1", coreConfig.CorePreferredMode == 1))
			{
				coreConfig.CorePreferredMode = 1;
			}
			ImGui::EndGroup();
		}
		ImGui::EndChild();
	}



	if (m_activeTab == TabStereo)
	{
		ImGui::BeginChild("Stereo#TabStereo");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status"))
		{
			if (m_displayValues.renderAPI == Vulkan)
			{
				ImGui::TextColored(colorTextRed, "Stereo reconstruction not supported under Vulkan!");
			}
			else if (mainConfig.ProjectionMode == ProjectionStereoReconstruction)
			{
				ImGui::TextColored(colorTextGreen, "Stereo reconstruction enabled");
			}
			else
			{
				ImGui::TextColored(colorTextRed, "Stereo reconstruction disabled");
			}

			ImGui::PushFont(m_smallFont);
			ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", m_displayValues.stereoReconstructionTimeMS);
			ImGui::PopFont();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Settings"))
		{
			TextDescription("Settings for the 3D stereo reconstuction.");
			ImGui::Checkbox("Use Multiple Cores", &stereoCustomConfig.StereoUseMulticore);
			TextDescription("Allows the stereo calculations to use multiple CPU cores. This can be turned off for CPU limited applications.");

			ScrollableSliderInt("Frame Skip Ratio", &stereoCustomConfig.StereoFrameSkip, 0, 14, "%d", 1);
			TextDescription("Skip stereo processing of this many frames for each frame processed. This does not affect the frame rate of viewed camera frames, every frame will still be reprojected on the latest stereo data.");

			ScrollableSliderInt("Image Downscale Factor", &stereoCustomConfig.StereoDownscaleFactor, 1, 16, "%d", 1);
			TextDescription("Ratio of the stereo processed image to the camera frame. Larger values will improve performance.");

			ImGui::BeginGroup();
			ImGui::Text("Filtering");
			if (ImGui::RadioButton("None###FiltNone", stereoCustomConfig.StereoFiltering == StereoFiltering_None))
			{
				stereoCustomConfig.StereoFiltering = StereoFiltering_None;
			}
			TextDescription("No filtering. Very noisy image with many invalid areas.");

			if (ImGui::RadioButton("Weighted Least Squares###FiltWLS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS))
			{
				stereoCustomConfig.StereoFiltering = StereoFiltering_WLS;
			}
			TextDescription("Patches up invalid areas. May still be noisy.");

			if (ImGui::RadioButton("Weighted Least Squares & Fast Bilateral Solver###FiltFBS", stereoCustomConfig.StereoFiltering == StereoFiltering_WLS_FBS))
			{
				stereoCustomConfig.StereoFiltering = StereoFiltering_WLS_FBS;
			}
			TextDescription("Patches up invalid areas, and filters the output. May produce worse depth results.");
			ImGui::EndGroup();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Advanced"))
		{
			ImGui::Checkbox("Rectification Filtering", &stereoCustomConfig.StereoRectificationFiltering);
			TextDescription("Applies linear filtering before stereo processing.");
			IMGUI_BIG_SPACING;

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Block Matching"))
			{
				/*ImGui::BeginGroup();
				ImGui::Text("Algorithm");
				if (ImGui::RadioButton("BM###AlgBM", sterestereoCustomConfigoConfig.StereoAlgorithm == StereoAlgorithm_BM))
				{
					stereoCustomConfig.StereoAlgorithm = StereoAlgorithm_BM;
				}
				if (ImGui::RadioButton("SGBM###AlgSGBM", stereoCustomConfig.StereoAlgorithm == StereoAlgorithm_SGBM))
				{
					stereoCustomConfig.StereoAlgorithm = StereoAlgorithm_SGBM;
				}
				ImGui::EndGroup();

				IMGUI_BIG_SPACING;*/

				ImGui::BeginGroup();
				ImGui::Text("SGBM Algorithm");
				if (ImGui::RadioButton("Single Pass: 3 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_SGBM3Way))
				{
					stereoCustomConfig.StereoSGBM_Mode = StereoMode_SGBM3Way;
				}
				if (ImGui::RadioButton("Single Pass: 5 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_SGBM))
				{
					stereoCustomConfig.StereoSGBM_Mode = StereoMode_SGBM;
				}
				if (ImGui::RadioButton("Full Pass: 4 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_HH4))
				{
					stereoCustomConfig.StereoSGBM_Mode = StereoMode_HH4;
				}
				if (ImGui::RadioButton("Full Pass: 8 Samples", stereoCustomConfig.StereoSGBM_Mode == StereoMode_HH))
				{
					stereoCustomConfig.StereoSGBM_Mode = StereoMode_HH;
				}
				ImGui::EndGroup();

				IMGUI_BIG_SPACING;

				ScrollableSliderInt("BlockSize", &stereoCustomConfig.StereoBlockSize, 1, 35, "%d", 2);
				if (stereoCustomConfig.StereoBlockSize % 2 == 0) { stereoCustomConfig.StereoBlockSize += 1; }

				//ScrollableSliderInt("MinDisparity", &stereoCustomConfig.StereoMinDisparity, 0, 128, "%d", 1);
				ScrollableSliderInt("MaxDisparity", &stereoCustomConfig.StereoMaxDisparity, 16, 256, "%d", 1);
				if (stereoCustomConfig.StereoMinDisparity % 2 != 0) { stereoCustomConfig.StereoMinDisparity += 1; }
				if (stereoCustomConfig.StereoMinDisparity >= stereoCustomConfig.StereoMaxDisparity) { stereoCustomConfig.StereoMaxDisparity = stereoCustomConfig.StereoMinDisparity + 2; }
				if (stereoCustomConfig.StereoMaxDisparity < 16) { stereoCustomConfig.StereoMaxDisparity = 16; }
				if (stereoCustomConfig.StereoMaxDisparity % 16 != 0) { stereoCustomConfig.StereoMaxDisparity -= stereoCustomConfig.StereoMaxDisparity % 16; }

				ScrollableSliderInt("SGBM P1", &stereoCustomConfig.StereoSGBM_P1, 0, 256, "%d", 8);
				ScrollableSliderInt("SGBM P2", &stereoCustomConfig.StereoSGBM_P2, 0, 256, "%d", 32);
				ScrollableSliderInt("SGBM DispMaxDiff", &stereoCustomConfig.StereoSGBM_DispMaxDiff, 0, 256, "%d", 1);
				ScrollableSliderInt("SGBM PreFilterCap", &stereoCustomConfig.StereoSGBM_PreFilterCap, 0, 128, "%d", 1);
				ScrollableSliderInt("SGBM UniquenessRatio", &stereoCustomConfig.StereoSGBM_UniquenessRatio, 1, 32, "%d", 1);
				ScrollableSliderInt("SGBM SpeckleWindowSize", &stereoCustomConfig.StereoSGBM_SpeckleWindowSize, 0, 300, "%d", 10);
				ScrollableSliderInt("SGBM SpeckleRange", &stereoCustomConfig.StereoSGBM_SpeckleRange, 1, 8, "%d", 1);

				ImGui::TreePop();
			}

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Filter Settings"))
			{
				ScrollableSlider("WLS Lambda", &stereoCustomConfig.StereoWLS_Lambda, 1.0f, 10000.0f, "%.0f", 100.0f);
				ScrollableSlider("WLS Sigma", &stereoCustomConfig.StereoWLS_Sigma, 0.5f, 2.0f, "%.1f", 0.1f);
				IMGUI_BIG_SPACING;

				ScrollableSlider("FBS Spatial", &stereoCustomConfig.StereoFBS_Spatial, 0.0f, 50.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Luma", &stereoCustomConfig.StereoFBS_Luma, 0.0f, 16.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Chroma", &stereoCustomConfig.StereoFBS_Chroma, 0.0f, 16.0f, "%.0f", 1.0f);
				ScrollableSlider("FBS Lambda", &stereoCustomConfig.StereoFBS_Lambda, 0.0f, 256.0f, "%.0f", 1.0f);

				ScrollableSliderInt("FBS Iterations", &stereoCustomConfig.StereoFBS_Iterations, 1, 35, "%d", 1);
				ImGui::TreePop();
			}
		}
		IMGUI_BIG_SPACING;

		stereoCustomConfig.StereoReconstructionFreeze = stereoConfig.StereoReconstructionFreeze;
		stereoConfig = stereoCustomConfig;

		ImGui::EndChild();
	}



	if (m_activeTab == TabOverrides)
	{
		ImGui::BeginChild("Overrides Pane");
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Overrides"))
		{
			ImGui::Checkbox("Force Passthrough Mode", &coreConfig.CoreForcePassthrough);
			TextDescription("Forces passthrough on even if the application does not support it.");

			ImGui::BeginGroup();
			if (ImGui::RadioButton("Alpha Blend###CoreForce3", coreConfig.CoreForceMode == 3))
			{
				coreConfig.CoreForceMode = 3;
			}
			TextDescription("Blends passthrough with application provided alpha mask. This requires application support.");
			if (ImGui::RadioButton("Additive###CoreForcef2", coreConfig.CoreForceMode == 2))
			{
				coreConfig.CoreForceMode = 2;
			}
			TextDescription("Adds passthrough and application output together.");
			if (ImGui::RadioButton("Opaque###Coreforce1", coreConfig.CoreForceMode == 1))
			{
				coreConfig.CoreForceMode = 1;
			}
			TextDescription("Replaces the application output with passthrough.");
			if (ImGui::RadioButton("Masked###Coreforce0", coreConfig.CoreForceMode == 0))
			{
				coreConfig.CoreForceMode = 0;
			}
			TextDescription("Blends passthrough with the application output using a chroma key mask.");
			ImGui::EndGroup();
			IMGUI_BIG_SPACING;
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Masked Croma Key Settings"))
		{
			ImGui::BeginChild("KeySettings", ImVec2(OVERLAY_RES_WIDTH * 0.48f, 0));
			ScrollableSlider("Chroma Range", &coreConfig.CoreForceMaskedFractionChroma, 0.0f, 1.0f, "%.2f", 0.01f);
			ScrollableSlider("Luma Range", &coreConfig.CoreForceMaskedFractionLuma, 0.0f, 1.0f, "%.2f", 0.01f);
			ScrollableSlider("Smoothing", &coreConfig.CoreForceMaskedSmoothing, 0.01f, 0.2f, "%.3f", 0.005f);
			ImGui::Checkbox("Invert mask", &coreConfig.CoreForceMaskedInvertMask);

			ImGui::BeginGroup();
			ImGui::Text("Chroma Key Source");
			if (ImGui::RadioButton("Application", !coreConfig.CoreForceMaskedUseCameraImage))
			{
				coreConfig.CoreForceMaskedUseCameraImage = false;
			}
			if (ImGui::RadioButton("Passthrough Camera", coreConfig.CoreForceMaskedUseCameraImage))
			{
				coreConfig.CoreForceMaskedUseCameraImage = true;
			}
			ImGui::EndGroup();

			ImGui::EndChild();
			ImGui::SameLine();

			ImGui::BeginChild("KeyPicker");
			ImGui::ColorPicker3("Key", coreConfig.CoreForceMaskedKeyColor, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar);
			ImGui::EndChild();
		}

		ImGui::EndChild();
	}



	if (m_activeTab == TabDebug)
	{
		ImGui::BeginChild("TabDebug");

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status"))
		{
			ImGui::PushFont(m_smallFont);
			ImGui::Text("Layer Version: %s", steamvr_passthrough::VersionString.c_str());

			switch (m_displayValues.renderAPI)
			{
			case DirectX11:
				ImGui::Text("Render API: DirectX 11");
				break;
			case DirectX12:
				ImGui::Text("Render API: DirectX 12");
				break;
			case Vulkan:
				ImGui::Text("Render API: Vulkan");
				break;
			default:
				ImGui::Text("Render API: None");
			}

			ImGui::Text("Application: %s", m_displayValues.currentApplication.c_str());
			ImGui::Text("Resolution: %i x %i", m_displayValues.frameBufferWidth, m_displayValues.frameBufferHeight);
			if (m_displayValues.frameBufferFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)
			{
				if (m_displayValues.frameBufferFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)
				{
					ImGui::Text("Flags: Unpremultiplied alpha");
				}
				else
				{
					ImGui::Text("Flags: Premultiplied alpha");
				}
			}
			else
			{
				ImGui::Text("Flags: No alpha");
			}

			ImGui::Text("Buffer format: %s (%li)", GetImageFormatName(m_displayValues.renderAPI, m_displayValues.frameBufferFormat).c_str(), m_displayValues.frameBufferFormat);

			ImGui::Text("Exposure to render latency: %.1fms", m_displayValues.frameToRenderLatencyMS);
			ImGui::Text("Exposure to photons latency: %.1fms", m_displayValues.frameToPhotonsLatencyMS);
			ImGui::Text("Passthrough CPU render duration: %.2fms", m_displayValues.renderTimeMS);
			ImGui::Text("Stereo reconstruction duration: %.2fms", m_displayValues.stereoReconstructionTimeMS);
			ImGui::PopFont();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Debug"))
		{
			ImGui::Checkbox("Freeze Stereo Projection", &stereoConfig.StereoReconstructionFreeze);
			ImGui::Checkbox("Debug Depth", &mainConfig.DebugDepth);
			ImGui::Checkbox("Debug Valid Stereo", &mainConfig.DebugStereoValid);
			ImGui::Checkbox("Show Test Image", &mainConfig.ShowTestImage);
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Log"))
		{
			ImGui::BeginChild("Log", ImVec2(0, 0), true);
			ImGui::Text("Log text goes here!");

			ImGui::EndChild();
		}
		ImGui::EndChild();
	}


	if (ImGui::IsAnyItemActive())
	{
		m_configManager->ConfigUpdated();
	}

	ImGui::End();

	ImGui::PopFont();
	ImGui::PopStyleVar(3);

	ImGui::Render();

	ID3D11RenderTargetView* rtv = m_d3d11RTV.Get();
	m_d3d11DeviceContext->OMSetRenderTargets(1, &rtv, NULL);
	const float clearColor[4] = { 0, 0, 0, 1 };
	m_d3d11DeviceContext->ClearRenderTargetView(m_d3d11RTV.Get(), clearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	m_d3d11DeviceContext->Flush();

	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Linear;
	texture.eType = vr::TextureType_DXGISharedHandle;

	ComPtr<IDXGIResource> DXGIResource;
	m_d3d11Texture->QueryInterface(IID_PPV_ARGS(&DXGIResource));
	DXGIResource->GetSharedHandle(&texture.handle);

	vr::EVROverlayError error = m_openVRManager->GetVROverlay()->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVR had an error on updating overlay (%d)\n", error);
	}
}


void DashboardMenu::CreateOverlay()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay)
	{
		return;
	}

	std::string overlayKey = std::format(DASHBOARD_OVERLAY_KEY, GetCurrentProcessId());

	vr::EVROverlayError error = vrOverlay->FindOverlay(overlayKey.c_str(), &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None && error != vr::EVROverlayError::VROverlayError_UnknownOverlay)
	{
		Log("Warning: SteamVR FindOverlay error (%d)\n", error);
	}

	if (m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		error = vrOverlay->CreateDashboardOverlay(overlayKey.c_str(), "OpenXR Passthrough", &m_overlayHandle, &m_thumbnailHandle);
		if (error != vr::EVROverlayError::VROverlayError_None)
		{
			ErrorLog("SteamVR overlay init error (%d)\n", error);
		}
		else
		{
			vrOverlay->SetOverlayInputMethod(m_overlayHandle, vr::VROverlayInputMethod_Mouse);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_IsPremultiplied, true);
			vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

			vrOverlay->SetOverlayWidthInMeters(m_overlayHandle, 2.775f);

			vr::HmdVector2_t ScaleVec;
			ScaleVec.v[0] = OVERLAY_RES_WIDTH;
			ScaleVec.v[1] = OVERLAY_RES_HEIGHT;
			vrOverlay->SetOverlayMouseScale(m_overlayHandle, &ScaleVec);

			CreateThumbnail();
		}
	}
}


void DashboardMenu::DestroyOverlay()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		m_overlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}

	vr::EVROverlayError error = vrOverlay->DestroyOverlay(m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None)
	{
		ErrorLog("SteamVR DestroyOverlay error (%d)\n", error);
	}

	m_overlayHandle = vr::k_ulOverlayHandleInvalid;
}


void DashboardMenu::CreateThumbnail()
{
	char path[MAX_PATH];

	if (FAILED(GetModuleFileNameA(m_dllModule, path, sizeof(path))))
	{
		ErrorLog("Error opening icon.\n");
		return;
	}

	std::string pathStr = path;
	std::string imgPath = pathStr.substr(0, pathStr.find_last_of("/\\")) + "\\passthrough_icon.png";
	
	m_openVRManager->GetVROverlay()->SetOverlayFromFile(m_thumbnailHandle, imgPath.c_str());
}


void DashboardMenu::HandleEvents()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay || m_overlayHandle == vr::k_ulOverlayHandleInvalid)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	vr::VREvent_t event;
	while (vrOverlay->PollNextOverlayEvent(m_overlayHandle, &event, sizeof(event)))
	{
		vr::VREvent_Overlay_t& overlayData = (vr::VREvent_Overlay_t&)event.data;
		vr::VREvent_Mouse_t& mouseData = (vr::VREvent_Mouse_t&)event.data;
		vr::VREvent_Scroll_t& scrollData = (vr::VREvent_Scroll_t&)event.data;

		switch (event.eventType)
		{
		case vr::VREvent_MouseButtonDown:

			if (mouseData.button & vr::VRMouseButton_Left)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			}
			if (mouseData.button & vr::VRMouseButton_Middle)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
			}
			if (mouseData.button & vr::VRMouseButton_Right)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
			}

			break;

		case vr::VREvent_MouseButtonUp:

			if (mouseData.button & vr::VRMouseButton_Left)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			}
			if (mouseData.button & vr::VRMouseButton_Middle)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
			}
			if (mouseData.button & vr::VRMouseButton_Right)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
			}

			break;

		case vr::VREvent_MouseMove:

			io.AddMousePosEvent(mouseData.x, OVERLAY_RES_HEIGHT - mouseData.y);

			break;

		case vr::VREvent_ScrollDiscrete:
		case vr::VREvent_ScrollSmooth:

			io.AddMouseWheelEvent(scrollData.xdelta, scrollData.ydelta);
			break;

		case vr::VREvent_FocusEnter:


			break;

		case vr::VREvent_FocusLeave:

			if (((vr::VREvent_Overlay_t&)event.data).overlayHandle == m_overlayHandle)
			{
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			}

			break;

		case vr::VREvent_OverlayShown:

			m_bMenuIsVisible = true;
			break;

		case vr::VREvent_OverlayHidden:

			m_bMenuIsVisible = false;
			m_configManager->DispatchUpdate();
			break;

		case vr::VREvent_DashboardActivated:
			//bThumbnailNeedsUpdate = true;
			break;

		}
	}
}


void DashboardMenu::SetupDX11()
{
	D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &m_d3d11Device, NULL, &m_d3d11DeviceContext);

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = OVERLAY_RES_WIDTH;
	textureDesc.Height = OVERLAY_RES_HEIGHT;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	m_d3d11Device->CreateTexture2D(&textureDesc, nullptr, &m_d3d11Texture);

	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	m_d3d11Device->CreateRenderTargetView(m_d3d11Texture.Get(), &renderTargetViewDesc, &m_d3d11RTV);
}
