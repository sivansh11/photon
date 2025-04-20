#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/model.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"

#include "photon/renderer.hpp"

#include <GLFW/glfw3.h>

class editor_camera_t {
public:
  editor_camera_t(core::window_t &window) : _window(window) {}
  void update_projection(float aspect_ratio);
  void update(float ts);

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

  float far{1000.f};
  float near{0.1f};

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

void editor_camera_t::update(float ts) {
  auto [width, height] = _window.dimensions();
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
  check(argc == 2, "photon [model path]");

  auto window = core::make_ref<core::window_t>("photon", 640, 420);
  auto context = core::make_ref<gfx::context_t>(true);
  auto base =
      core::make_ref<gfx::base_t>(gfx::base_config_t{*window, *context});

  auto dispatcher = core::make_ref<core::dispatcher_t>();

  auto current_scene = core::make_ref<ecs::scene_t<>>();
  {
    auto id = current_scene->create();
    current_scene->construct<core::raw_model_t>(id) =
        core::load_model_from_path(argv[1]);
    current_scene->construct<core::transform_t>(id);
  }

  photon::renderer_t renderer{640, 420, window, context, base, dispatcher};

  editor_camera_t editor_camera{*window};

  while (!window->should_close()) {
    core::window_t::poll_events();
    if (glfwGetKey(*window, GLFW_KEY_ESCAPE))
      break;

    core::camera_t camera{
        .view = editor_camera.view(),
        .projection = editor_camera.projection(),
    };

    renderer.render(current_scene, camera);
  }

  context->wait_idle();

  return 0;
}
