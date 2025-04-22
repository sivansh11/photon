#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/model.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"

#include "horizon/gfx/helper.hpp"
#include "horizon/gfx/types.hpp"
#include "imgui.h"
#include "photon/renderer.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

class editor_camera_t {
public:
  editor_camera_t(core::window_t &window) : _window(window) {}
  void update_projection(float aspect_ratio);
  void update(float ts, uint32_t width, uint32_t height);

  const core::mat4 &projection() const { return _projection; }
  const core::mat4 &view() const { return _view; }
  // creates a new mat4
  core::mat4 inverse_projection() const { return core::inverse(_projection); }
  // creates a new mat4
  core::mat4 inverse_view() const { return core::inverse(_view); }
  core::vec3 front() const { return _front; }
  core::vec3 right() const { return _right; }
  core::vec3 up() const { return _up; }
  core::vec3 position() const { return _position; }

public:
  float fov{90.0f}; // zoom var ?
  float camera_speed_multiplyer{1};

  float far{10000.f};
  float near{0.001f};

private:
  core::window_t &_window;

  core::vec3 _position{0, 2, 0};

  core::mat4 _projection{1.0f};
  core::mat4 _view{1.0f};

  core::vec3 _front{1, 0, 0};
  core::vec3 _up{0, 1, 0};
  core::vec3 _right{1, 0, 0};

  core::vec2 _initial_mouse{};

  float _yaw{0};
  float _pitch{0};
  float _mouse_speed{0.005f};
  float _mouse_sensitivity{100.f};
};

void editor_camera_t::update_projection(float aspect_ratio) {
  static float s_aspect_ratio = 0;
  if (s_aspect_ratio != aspect_ratio) {
    _projection =
        core::perspective(core::radians(fov), aspect_ratio, near, far);
    s_aspect_ratio = aspect_ratio;
  }
}

void editor_camera_t::update(float ts, uint32_t width, uint32_t height) {
  // auto [width, height] = _window.dimensions();
  update_projection(float(width) / float(height));

  double curX, curY;
  glfwGetCursorPos(_window.window(), &curX, &curY);

  float velocity = _mouse_speed * ts;

  if (glfwGetKey(_window.window(), GLFW_KEY_W))
    _position += _front * velocity;
  if (glfwGetKey(_window.window(), GLFW_KEY_S))
    _position -= _front * velocity;
  if (glfwGetKey(_window.window(), GLFW_KEY_D))
    _position += _right * velocity;
  if (glfwGetKey(_window.window(), GLFW_KEY_A))
    _position -= _right * velocity;
  if (glfwGetKey(_window.window(), GLFW_KEY_SPACE))
    _position += _up * velocity;
  if (glfwGetKey(_window.window(), GLFW_KEY_LEFT_SHIFT))
    _position -= _up * velocity;

  core::vec2 mouse{curX, curY};
  core::vec2 difference = mouse - _initial_mouse;
  _initial_mouse = mouse;

  if (glfwGetMouseButton(_window.window(), GLFW_MOUSE_BUTTON_1)) {

    difference.x = difference.x / float(width);
    difference.y = -(difference.y / float(height));

    _yaw += difference.x * _mouse_sensitivity;
    _pitch += difference.y * _mouse_sensitivity;

    if (_pitch > 89.0f)
      _pitch = 89.0f;
    if (_pitch < -89.0f)
      _pitch = -89.0f;
  }

  core::vec3 front;
  front.x = core::cos(core::radians(_yaw)) * core::cos(core::radians(_pitch));
  front.y = core::sin(core::radians(_pitch));
  front.z = core::sin(core::radians(_yaw)) * core::cos(core::radians(_pitch));
  _front = front * camera_speed_multiplyer;
  _right = core::normalize(core::cross(_front, core::vec3{0, 1, 0}));
  _up = core::normalize(core::cross(_right, _front));

  _view = core::lookAt(_position, _position + _front, core::vec3{0, 1, 0});
}

int main(int argc, char **argv) {
  check(argc == 3, "photon [photon assets path] [model path]");

  uint32_t width = 640, height = 420;

  auto window = core::make_ref<core::window_t>("photon", width, height);
  auto context = core::make_ref<gfx::context_t>(true);
  auto base =
      core::make_ref<gfx::base_t>(gfx::base_config_t{*window, *context});
  gfx::helper::imgui_init(*window, *context, base->_swapchain,
                          VK_FORMAT_B8G8R8A8_SRGB);

  // TODO: think if 0 sampler should be default
  gfx::handle_sampler_t sampler = context->create_sampler({});
  gfx::handle_bindless_sampler_t bsampler = base->new_bindless_sampler();
  base->set_bindless_sampler(bsampler, sampler);

  auto dispatcher = core::make_ref<core::dispatcher_t>();

  auto current_scene = core::make_ref<ecs::scene_t<>>();
  {
    auto id = current_scene->create();
    current_scene->construct<core::raw_model_t>(id) =
        core::load_model_from_path(argv[2]);
    auto &transform = current_scene->construct<core::transform_t>(id);
    transform.scale = {0.01, 0.01, 0.01};
  }

  photon::renderer_t renderer{width, height,     window, context,
                              base,  dispatcher, argv[1]};

  editor_camera_t editor_camera{*window};
  editor_camera.update_projection(float(width) / float(height));

  // TODO: put this in helper
  gfx::config_descriptor_set_layout_t
      config_imgui_image_descriptor_set_layout{};
  config_imgui_image_descriptor_set_layout.use_bindless = false;
  config_imgui_image_descriptor_set_layout.add_layout_binding(
      0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_SHADER_STAGE_FRAGMENT_BIT);
  auto imgui_image_descriptor_set_layout =
      context->create_descriptor_set_layout(
          config_imgui_image_descriptor_set_layout);
  auto imgui_image_descriptor_set = context->allocate_descriptor_set(
      {.handle_descriptor_set_layout = imgui_image_descriptor_set_layout});
  gfx::handle_image_view_t main_image_view = core::null_handle;

  float target_fps = 10000000.f;
  auto last_time = std::chrono::system_clock::now();
  core::frame_timer_t frame_timer{60.f};

  bool resize = false;

  while (!window->should_close()) {
    core::window_t::poll_events();
    if (glfwGetKey(*window, GLFW_KEY_ESCAPE))
      break;

    auto current_time = std::chrono::system_clock::now();
    auto time_difference = current_time - last_time;
    if (time_difference.count() / 1e6 < 1000.f / target_fps) {
      continue;
    }
    last_time = current_time;
    auto dt = frame_timer.update();

    editor_camera.update(dt.count(), width, height);
    // TODO: add to_string operator << overloads
    // horizon_info("{}", core::to_string(editor_camera.position()));

    core::camera_t camera{
        .view = editor_camera.view(),
        .projection = editor_camera.projection(),
    };

    base->begin();

    auto image_view = renderer.render(current_scene, camera);

    base->begin_swapchain_renderpass();

    gfx::helper::imgui_newframe();

    ImGuiDockNodeFlags dockspaceFlags =
        ImGuiDockNodeFlags_None & ~ImGuiDockNodeFlags_PassthruCentralNode;
    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoDecoration;

    bool dockSpace = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    auto mainViewPort = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewPort->WorkPos);
    ImGui::SetNextWindowSize(mainViewPort->WorkSize);
    ImGui::SetNextWindowViewport(mainViewPort->ID);

    ImGui::Begin("DockSpace", &dockSpace, windowFlags);
    ImGuiID dockspaceID = ImGui::GetID("DockSpace");
    ImGui::DockSpace(dockspaceID, ImGui::GetContentRegionAvail(),
                     dockspaceFlags);
    if (ImGui::BeginMainMenuBar()) {
      ImGui::EndMainMenuBar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowClass window_class;
    window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&window_class);
    ImGuiWindowFlags viewPortFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration;
    ImGui::Begin("viewport", nullptr, viewPortFlags);

    if (image_view != main_image_view) {
      main_image_view = image_view;
      context->update_descriptor_set(imgui_image_descriptor_set)
          .push_image_write(
              0, {.handle_sampler = sampler,
                  .handle_image_view = image_view,
                  .vk_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
          .commit();
    }
    auto size = ImGui::GetContentRegionAvail();
    if (size.x != width || size.y != height) {
      width = size.x;
      height = size.y;
    }
    ImGui::Image(reinterpret_cast<ImTextureID>(reinterpret_cast<void *>(
                     context->get_descriptor_set(imgui_image_descriptor_set)
                         .vk_descriptor_set)),
                 ImGui::GetContentRegionAvail(), {0, 1}, {1, 0});

    ImGui::End();
    ImGui::PopStyleVar(2);

    gfx::helper::imgui_endframe(*context, base->current_commandbuffer());

    base->end_swapchain_renderpass();

    base->end();

    if (resize) {
      resize = false;
      context->wait_idle();
      dispatcher->post<photon::resize_event_t>(
          photon::resize_event_t{.width = width, .height = height});
    }
  }

  context->wait_idle();

  gfx::helper::imgui_shutdown();

  return 0;
}
