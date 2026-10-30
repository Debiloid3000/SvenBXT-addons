#pragma once
#include "movement_math.hpp"

namespace sbxtma {
constexpr unsigned short JumpButton = 1 << 1;
constexpr unsigned short DuckButton = 1 << 2;
struct Forecast {
    bool valid = false, edge = false, wall = false;
    int groundTick = 0;
};
struct MovementState {
    bool valid = false, walking = false, grounded = false, multiplayer = false;
    bool pushable = false;
    Vec3 velocity;
    float maxspeed = 320, airaccelerate = 10;
    Forecast forecast;
};
struct Controls {
    unsigned short buttons = 0;
    float forward = 0, side = 0, yaw = 0;
};
struct AssistSettings {
    int mode = 2; // 0 off, 1 detection, 2 assist
    bool online = true, edge = true, jump = true, strafe = true, pixel = true;
};
enum MovementEvent { NoEvent = 0, Edge = 1, Jumpbug = 2, Pixelsurf = 4 };
class AssistController {
    bool jumpArmed = false;
public:
    void reset() { jumpArmed = false; }
    int apply(const MovementState& state, Controls& cmd, const AssistSettings& cfg, float dt) {
        if (!state.valid || !state.walking || cfg.mode == 0) { reset(); return NoEvent; }
        const bool assist = cfg.mode >= 2 && (!state.multiplayer || cfg.online);
        if (state.grounded) { reset(); return NoEvent; }
        int events = NoEvent;
        const auto& f = state.forecast;
        if (cfg.edge && f.valid && f.edge && state.velocity.z < -180) {
            if (assist) cmd.buttons |= DuckButton;
            events |= Edge;
        }
        if (cfg.jump && f.valid && f.groundTick && state.velocity.z < -180) {
            if (assist) {
                if (f.groundTick > 1) {
                    cmd.buttons = (cmd.buttons | DuckButton) & ~JumpButton;
                    jumpArmed = true;
                } else if (jumpArmed || (cmd.buttons & DuckButton)) {
                    cmd.buttons = (cmd.buttons | JumpButton) & ~DuckButton;
                    jumpArmed = false;
                }
            }
            events |= Jumpbug;
        } else jumpArmed = false;
        if (cfg.strafe && assist && state.velocity.speed2d() >= 10 &&
            (std::fabs(cmd.forward) > 1 || std::fabs(cmd.side) > 1)) {
            const float wish = clampf(state.maxspeed, 1, 1000);
            const float angle = ideal_strafe_angle(wish, state.airaccelerate, dt,
                                                   state.velocity.speed2d());
            // GoldSrc's positive sidemove points RIGHT, i.e. negative world Y
            // at yaw=0. Keep viewangles unchanged and rotate the wish vector.
            const float direction = cmd.side < 0 ? 1.f : -1.f;
            const float target = std::atan2(state.velocity.y, state.velocity.x) +
                                 direction * radians(angle);
            cmd.forward = wish * std::cos(target - radians(cmd.yaw));
            cmd.side = -wish * std::sin(target - radians(cmd.yaw));
        }
        if (cfg.pixel && f.valid && f.wall && std::fabs(state.velocity.z) < 35) {
            if (assist) cmd.forward = cmd.side = 0;
            events |= Pixelsurf;
        }
        return events;
    }
};
} // namespace sbxtma
