#include "bop_state.h"
#include <ArduinoLowPower.h>
#include <FlashStorage.h>

BopState *current_state;
bool BopState::last_fail_was_timeout = false;

// After this many restarts in a row, GameStartState sleeps until woken.
static const uint8_t SLEEP_AFTER_RESTARTS = 3;
// How often the sleeping board wakes briefly to blink LED_BUILTIN.
static const uint32_t SLEEP_HEARTBEAT_MS = 10000;

// Set by the button interrupt so the sleep loop can tell "woken by the button"
static volatile bool woke_from_button = false;
static void wake_isr() { woke_from_button = true; }

// Freshly-flashed boards read back 0xFFFF, so that's
// treated as "no score yet" rather than an actual high score.
FlashStorage(high_score_store, uint16_t);
static const uint16_t NO_HIGH_SCORE = 0xFFFF;

void BopState::set_state()
{
    // current_state must be updated before prepare_state() runs: a state
    // (like GameStartState) may call succeed() from inside prepare_state(),
    // which recursively calls set_state() on another state. Setting
    // current_state afterwards here would break that nested assignment
    if (current_state != nullptr && current_state != this)
    {
        current_state->on_leave();
    }
    current_state = this;
    start_millis = millis();
    prepare_state();
}

void BopState::render()
{
    if (millis() - start_millis > timeout_millis)
    {
        fail(true);
    }
}

void BopState::succeed()
{
    if (next_states == nullptr || next_state_count == 0)
        return;
    uint8_t idx = random(next_state_count);
    next_states[idx]->set_state();
}

void BopState::fail(bool was_timeout)
{
    last_fail_was_timeout = was_timeout;
    if (fail_state != nullptr)
    {
        fail_state->set_state();
    }
}

static const uint32_t BASE_TIMEOUT_MS = 2000;
static const uint32_t MIN_TIMEOUT_MS = 900;
static const uint32_t TIMEOUT_STEP_MS = 150;

uint32_t PromptState::next_timeout_ms = BASE_TIMEOUT_MS;
uint16_t PromptState::score = 0;

void PromptState::reset_difficulty()
{
    next_timeout_ms = BASE_TIMEOUT_MS;
}

void PromptState::succeed()
{
    score++;
    BopState::succeed();
}

void PromptState::prepare_state()
{
    Serial.print("Prompt: ");
    Serial.println(name);
    chirp_done = false;

    set_timeout_millis(next_timeout_ms);
    if (next_timeout_ms > MIN_TIMEOUT_MS + TIMEOUT_STEP_MS)
    {
        next_timeout_ms -= TIMEOUT_STEP_MS;
    }
    else
    {
        next_timeout_ms = MIN_TIMEOUT_MS;
    }
}

void PromptState::render()
{
    if (!chirp_done)
    {
        const uint32_t beep_ms = 60;
        const uint32_t gap_ms = 60;
        uint32_t elapsed = millis() - start_millis;
        if (elapsed < beep_ms)
        {
            tone(buzzer_pin, chirp_freq);
        }
        else if (elapsed < beep_ms + gap_ms)
        {
            noTone(buzzer_pin);
        }
        else if (elapsed < beep_ms * 2 + gap_ms)
        {
            tone(buzzer_pin, chirp_freq);
        }
        else
        {
            noTone(buzzer_pin);
            chirp_done = true;
        }
    }

    update_leds();

    BopState::render();
}

void PromptState::update_leds()
{
    const uint32_t blink_interval_ms = 80;
    bool led_on = ((millis() - start_millis) / blink_interval_ms) % 2 == 0;
    set_led(led_on);
}

void PromptState::on_leave()
{
    if (!chirp_done)
    {
        noTone(buzzer_pin);
        chirp_done = true;
    }
    set_led(false);
}

void SlidePromptState::update_leds()
{
    const uint32_t step_ms = 90;
    uint8_t step = ((millis() - start_millis) / step_ms) % led_count;
    uint8_t lit = reverse ? (led_count - 1 - step) : step;
    for (uint8_t i = 0; i < led_count; i++)
    {
        digitalWrite(led_pins[i], i == lit ? HIGH : LOW);
    }
}

void RoundGapState::prepare_state()
{
    if (random(100) < long_pause_percent)
    {
        delay_ms = random(long_min_ms, long_max_ms + 1);
    }
    else
    {
        delay_ms = random(min_ms, max_ms + 1);
    }
}

void RoundGapState::render()
{
    if (millis() - start_millis >= delay_ms)
    {
        succeed();
    }
}

void GameStartState::prepare_state()
{
    if (first_call)
    {
        first_call = false;
        restart_count = 0;
    }
    else if (last_fail_was_timeout)
    {
        restart_count++;
    }
    else
    {
        // A wrong input means someone was actively playing - don't count
        // this restart toward sleeping, and don't let it sneak in after a
        // streak of idle timeouts either.
        restart_count = 0;
    }

    if (restart_count >= SLEEP_AFTER_RESTARTS)
    {
        restart_count = 0;
        Serial.println("Sleeping to save power - press the button to wake.");
        Serial.flush();

        LowPower.attachInterruptWakeup(digitalPinToInterrupt(wake_pin), wake_isr, RISING);
        woke_from_button = false;
        while (!woke_from_button)
        {
            // Wakes early if the button interrupt fires; otherwise wakes on
            // its own after SLEEP_HEARTBEAT_MS to blink and go back down.
            LowPower.sleep(SLEEP_HEARTBEAT_MS);
            if (!woke_from_button)
            {
                digitalWrite(LED_BUILTIN, HIGH);
                delay(50);
                digitalWrite(LED_BUILTIN, LOW);
            }
        }
    }

    PromptState::reset_difficulty();
    PromptState::reset_score();

    Serial.println("Get ready!");
    const uint16_t melody[] = {523, 659, 784, 1047}; // C5 E5 G5 C6
    const uint16_t note_ms = 150;
    for (uint16_t freq : melody)
    {
        tone(buzzer_pin, freq, note_ms);
        delay(note_ms);
    }
    noTone(buzzer_pin);
    succeed();
}

void GameOverState::prepare_state()
{
    uint16_t score = PromptState::get_score();
    uint16_t high_score = high_score_store.read();
    if (high_score == NO_HIGH_SCORE || score > high_score)
    {
        high_score = score;
        high_score_store.write(high_score);
    }

    Serial.print("Game Over! Score: ");
    Serial.print(score);
    Serial.print("  Best: ");
    Serial.println(high_score);
    tone(buzzer_pin, 100, 500);
}
