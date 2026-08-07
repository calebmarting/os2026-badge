#pragma once

#include <Arduino.h>

// BopState is the game currently waiting on the player to do like waiting for the button.
//  The sensors in main.cpp's loop() call the matching on_*() method on
//  current_state whenever an input is detected.
class BopState
{
public:
    virtual ~BopState() {}
    BopState() {}

    // Called every loop(). Default implementation fails the round once timeout_millis has elapsed
    virtual void render();

    // Input events. Default behavior for all five is "wrong input -> fail".
    virtual void on_slide_left() { fail(); }
    virtual void on_slide_right() { fail(); }
    virtual void on_button() { fail(); }
    virtual void on_microphone() { fail(); }
    virtual void on_whack() { fail(); }

    // Makes this the active state.
    void set_state();

    // Pool of states succeed() may randomly advance to.
    void set_next_states(BopState *const *states, uint8_t count)
    {
        next_states = states;
        next_state_count = count;
    }
    // State to switch to on a wrong input or timeout.
    void set_fail_state(BopState *state) { fail_state = state; }
    void set_timeout_millis(uint32_t ms) { timeout_millis = ms; }

protected:
    // Called once on entry - reset per-round timers, arm LEDs, etc.
    virtual void prepare_state() = 0;
    // Called on the currently-active state right before switching away
    virtual void on_leave() {}

    // Correct input arrived: hand off to a random state from next_states.
    void succeed();
    // Wrong input or timeout so it can hand off to the correct next fail state
    void fail(bool was_timeout = false);

    BopState *fail_state = nullptr;
    BopState *const *next_states = nullptr;
    uint8_t next_state_count = 0;
    uint32_t timeout_millis = 3000;
    uint32_t start_millis = 0;

    // Whether the most recent fail() anywhere in the state machine was a
    // response-time timeout (true) or a wrong input (false).
    static bool last_fail_was_timeout;
};

// Common base for every state that prompts the player input. Blinks indicator, and plays a quick double chirp
class PromptState : public BopState
{
public:
    PromptState(const char *name, uint8_t led_pin, uint8_t buzzer_pin, uint16_t chirp_freq)
        : name(name), led_pin(led_pin), buzzer_pin(buzzer_pin), chirp_freq(chirp_freq) {}

    void render() override;

    // Puts the response-time ramp back at its starting (most generous)
    // value. Call this when a fresh game begins.
    static void reset_difficulty();

    // Number of prompts successfully completed so far this game.
    static uint16_t get_score() { return score; }
    // Zeroes the round counter. Call this when a fresh game begins.
    static void reset_score() { score = 0; }

protected:
    void prepare_state() override;
    void on_leave() override;
    // Drives this prompt's indicator LED(s). Default drives the single led_pin override for e.g. all the touch-pad slider LEDs at once.
    virtual void set_led(bool on) { digitalWrite(led_pin, on ? HIGH : LOW); }
    // Called every render() to animate the LED(s) while waiting.
    virtual void update_leds();
    // Hides BopState::succeed(): bumps the shared score, then defers to it.
    void succeed();

    const char *name;
    uint8_t led_pin;
    uint8_t buzzer_pin;
    uint16_t chirp_freq;
    bool chirp_done = false;

    // how long the next prompt gets to respond.
    static uint32_t next_timeout_ms;
    static uint16_t score;
};

// Common base for the two directional slide prompts
class SlidePromptState : public PromptState
{
public:
    SlidePromptState(const char *name, const uint8_t *led_pins, uint8_t led_count, bool reverse, uint8_t buzzer_pin, uint16_t chirp_freq)
        : PromptState(name, led_pins[0], buzzer_pin, chirp_freq), led_pins(led_pins), led_count(led_count), reverse(reverse) {}

protected:
    void update_leds() override;
    void set_led(bool on) override
    {
        for (uint8_t i = 0; i < led_count; i++)
        {
            digitalWrite(led_pins[i], on ? HIGH : LOW);
        }
    }

    const uint8_t *led_pins;
    uint8_t led_count;
    bool reverse;
};

class SlideLeftPromptState : public SlidePromptState
{
public:
    using SlidePromptState::SlidePromptState;
    void on_slide_left() override { succeed(); }
};

class SlideRightPromptState : public SlidePromptState
{
public:
    using SlidePromptState::SlidePromptState;
    void on_slide_right() override { succeed(); }
};

class ButtonPromptState : public PromptState
{
public:
    using PromptState::PromptState;
    void on_button() override { succeed(); }
};

class MicrophonePromptState : public PromptState
{
public:
    using PromptState::PromptState;
    void on_microphone() override { succeed(); }
};

class WhackPromptState : public PromptState
{
public:
    using PromptState::PromptState;
    void on_whack() override { succeed(); }
};

// Brief pause after a successful prompt, before the next random prompt
class RoundGapState : public BopState
{
public:
    RoundGapState(uint16_t min_ms, uint16_t max_ms, uint16_t long_min_ms, uint16_t long_max_ms, uint8_t long_pause_percent)
        : min_ms(min_ms), max_ms(max_ms), long_min_ms(long_min_ms), long_max_ms(long_max_ms), long_pause_percent(long_pause_percent) {}
    void render() override;
    void on_slide_left() override {}
    void on_slide_right() override {}
    void on_button() override {}
    void on_microphone() override {}
    void on_whack() override {}

protected:
    void prepare_state() override;
    uint16_t min_ms;
    uint16_t max_ms;
    uint16_t long_min_ms;
    uint16_t long_max_ms;
    uint8_t long_pause_percent;
    uint16_t delay_ms = 0;
};

// Idle state shown at power-on. Plays a short tune, then hands off to a
// random state from next_states
class GameStartState : public BopState
{
public:
    GameStartState(uint8_t buzzer_pin, uint8_t wake_pin) : buzzer_pin(buzzer_pin), wake_pin(wake_pin) {}
    void render() override {} // prepare_state plays the tune and advances itself

protected:
    void prepare_state() override;
    uint8_t buzzer_pin;
    uint8_t wake_pin;
    bool first_call = true;
    uint8_t restart_count = 0;
};

// Mad cuz bad get rekt
class GameOverState : public BopState
{
public:
    GameOverState(uint8_t buzzer_pin) : buzzer_pin(buzzer_pin) {}
    void on_button() override { succeed(); }

protected:
    void prepare_state() override;
    uint8_t buzzer_pin;
};

extern BopState *current_state;
