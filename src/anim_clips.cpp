#include "anim_clips.h"

#include "app_state.h"
#include "graphics_pet_presentation.h"
#include "pet.h"
#include "pet_state.h"
#include "ui_runtime.h"
#include <Arduino.h>

// We intentionally keep clip paths centralized here.
// This is the “one true source” for asset routing.

// ---------------------------
// Devil baby HAPPY Ball (your new animation)
// (moved from graphics.cpp into the clip registry)
// ---------------------------

static const char *kDevBabyHappyBall[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/hpy/dev_baby_hpy_ball1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/hpy/dev_baby_hpy_ball2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/hpy/dev_baby_hpy_ball3.png",
    "/raising_hell/graphics/pet/anim/dev/bb/hpy/dev_baby_hpy_ball4.png",
};

// ---------------------------
// Devil baby HUNGRY rub (your new 2-frame animation)
// ---------------------------
static const char *kDevBabyHungryRub[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/hgy/dev_baby_hgy_rub1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/hgy/dev_baby_hgy_rub2.png",
};

// ---------------------------
// Devil baby BORED ball play (4-frame)
// ---------------------------
static const char *kDevBabyBoredRoll[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/brd/dev_baby_brd_roll1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/brd/dev_baby_brd_roll2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/brd/dev_baby_brd_roll3.png",
    "/raising_hell/graphics/pet/anim/dev/bb/brd/dev_baby_brd_roll4.png",
};

// ---------------------------
// Devil baby ANGRY (ported from pet_anim.cpp)
// ---------------------------
static const char *kDevBabyAngryBob[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_bob1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_bob2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_bob3.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_bob4.png",
};

static const char *kDevBabyAngrySwipe[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_swipe1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_swipe2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_swipe3.png",
    "/raising_hell/graphics/pet/anim/dev/bb/ang/dev_baby_swipe4.png",
};

// ---------------------------
// Devil baby SLEEPY
// ---------------------------
static const char *kDevBabySleepyNod[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/slp/dev_baby_slp_nod1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/slp/dev_baby_slp_nod2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/slp/dev_baby_slp_nod3.png",
};

// ---------------------------
// Devil baby SICK
// ---------------------------
static const char *kDevBabySickCrawl[] = {
    "/raising_hell/graphics/pet/anim/dev/bb/sck/dev_baby_sck_crawl1.png",
    "/raising_hell/graphics/pet/anim/dev/bb/sck/dev_baby_sck_crawl2.png",
    "/raising_hell/graphics/pet/anim/dev/bb/sck/dev_baby_sck_crawl3.png",
};

// ---------------------------
// Devil teen HAPPY play (your new 3-frame animation)
// ---------------------------
static const char *kDevTeenHappyPose[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/hpy/dev_tn_hpy_play1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/hpy/dev_tn_hpy_play2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/hpy/dev_tn_hpy_play3.png",
};

// ---------------------------
// Devil teen HUNGRY rub (your new 2-frame animation)
// ---------------------------
static const char *kDevTeenHungryRub[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/hgy/dev_teen_hgy_rub1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/hgy/dev_teen_hgy_rub2.png",
};

// ---------------------------
// Devil teen ANGRY claw attack (4-frame)
// ---------------------------
static const char *kDevTeenAngryClaw[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/ang/dev_teen_ang_claw1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/ang/dev_teen_ang_claw2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/ang/dev_teen_ang_claw3.png",
    "/raising_hell/graphics/pet/anim/dev/tn/ang/dev_teen_ang_claw4.png",
};

// ---------------------------
// Devil teen SLEEPY bob (4-frame)
// ---------------------------
static const char *kDevTeenSleepyBob[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/slp/dev_teen_slp_bob1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/slp/dev_teen_slp_bob2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/slp/dev_teen_slp_bob3.png",
    "/raising_hell/graphics/pet/anim/dev/tn/slp/dev_teen_slp_bob4.png",
};

// ---------------------------
// Devil teen SICK bob (4-frame)
// ---------------------------
static const char *kDevTeenSickBob[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/sck/dev_teen_sck_bob1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/sck/dev_teen_sck_bob2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/sck/dev_teen_sck_bob3.png",
    "/raising_hell/graphics/pet/anim/dev/tn/sck/dev_teen_sck_bob4.png",
};

// ---------------------------
// Devil teen BORED play (4-frame)
// ---------------------------
static const char *kDevTeenBoredPlay[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_tn_brd_drink1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_tn_brd_drink2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_tn_brd_drink3.png",
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_tn_brd_drink4.png",
};

// ---------------------------
// Devil adult HAPPY tail wag
// ---------------------------
static const char *kDevAdultHappyTail[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/hpy/dev_ad_hpy_tail1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hpy/dev_ad_hpy_tail2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hpy/dev_ad_hpy_tail3.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hpy/dev_ad_hpy_tail2.png",
};

// ---------------------------
// Devil adult HUNGRY bend (4-frame)
// ---------------------------
static const char *kDevAdultHungryBend[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/hgy/dev_ad_hgy_bend1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hgy/dev_ad_hgy_bend2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hgy/dev_ad_hgy_bend3.png",
    "/raising_hell/graphics/pet/anim/dev/ad/hgy/dev_ad_hgy_bend4.png",
};

// ---------------------------
// Devil adult ANGRY stance (2-frame)
// ---------------------------
static const char *kDevAdultAngryStance[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_stance1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_stance2.png",
};

// ---------------------------
// Devil adult ANGRY kick (4-frame)
// ---------------------------
static const char *kDevAdultAngryKick[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_kick1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_kick2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_kick3.png",
    "/raising_hell/graphics/pet/anim/dev/ad/agy/dev_ad_ang_kick4.png",
};

// ---------------------------
// Devil adult BORED gameboy (3-frame)
// ---------------------------
static const char *kDevAdultBoredGameboy[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/brd/dev_ad_brd_gameboy1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/brd/dev_ad_brd_gameboy2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/brd/dev_ad_brd_gameboy3.png",
};

// ---------------------------
// Devil adult TIRED chair (idle hold + blink)
// ---------------------------
static const char *kDevAdultTiredChairIdle[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/trd/dev_ad_trd_chair1.png",
};

static const char *kDevAdultTiredChairBlink[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/trd/dev_ad_trd_chair2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/trd/dev_ad_trd_chair3.png",
};

// ---------------------------
// Devil adult SICK (laying down)
// ---------------------------
static const char *kDevAdultSickLay[] = {
    "/raising_hell/graphics/pet/anim/dev/ad/sck/dev_ad_sck_barf1.png",
    "/raising_hell/graphics/pet/anim/dev/ad/sck/dev_ad_sck_barf2.png",
    "/raising_hell/graphics/pet/anim/dev/ad/sck/dev_ad_sck_barf3.png",
    "/raising_hell/graphics/pet/anim/dev/ad/sck/dev_ad_sck_barf4.png",
};

// ---------------------------
// Devil elder BORED spin (5-frame)
// ---------------------------
static const char *kDevElderBoredSpin[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin2.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin3.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin4.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin5.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/brd/dev_edr_spin1.png",
};

// ---------------------------
// Devil elder ANGRY fire (4-frame)
// ---------------------------
static const char *kDevElderAngryFire[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/agy/dev_el_angry_fire1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/agy/dev_el_angry_fire2.png",
    "/raising_hell/graphics/pet/anim/dev/ed/agy/dev_el_angry_fire3.png",
    "/raising_hell/graphics/pet/anim/dev/ed/agy/dev_el_angry_fire4.png",
};

// ---------------------------
// Devil elder HAPPY shake (4-frame)
// ---------------------------
static const char *kDevElderHappyShake[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/hpy/dev_elder_hpy_shake1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/hpy/dev_elder_hpy_shake2.png",
    "/raising_hell/graphics/pet/anim/dev/ed/hpy/dev_elder_hpy_shake3.png",
    "/raising_hell/graphics/pet/anim/dev/ed/hpy/dev_elder_hpy_shake4.png",
};

// ---------------------------
// Devil elder HUNGRY rub (3-frame)
// ---------------------------
static const char *kDevElderHungryRub[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/hgy/dev_el_hgy_rub1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/hgy/dev_el_hgy_rub2.png",
    "/raising_hell/graphics/pet/anim/dev/ed/hgy/dev_el_hgy_rub3.png",
};

// ---------------------------
// Devil elder TIRED sit (2-frame head bob)
// ---------------------------
static const char *kDevElderTiredSit[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/trd/dev_edr_trd_sit1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/trd/dev_edr_trd_sit2.png",
};

// ---------------------------
// Devil elder SICK cough (3-frame)
// ---------------------------
static const char *kDevElderSickCough[] = {
    "/raising_hell/graphics/pet/anim/dev/ed/sck/dev_edr_sck_cough1.png",
    "/raising_hell/graphics/pet/anim/dev/ed/sck/dev_edr_sck_cough2.png",
    "/raising_hell/graphics/pet/anim/dev/ed/sck/dev_edr_sck_cough3.png",
};

// ---------------------------
// Eldritch baby HAPPY sit (4-frame)
// ---------------------------
static const char *kEldBabyHappySit[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/hpy/eld_baby_hpy_sit1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/hpy/eld_baby_hpy_sit2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/hpy/eld_baby_hpy_sit3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/hpy/eld_baby_hpy_sit4.png",
};

// ---------------------------
// Eldritch baby HUNGRY rub (3-frame)
// ---------------------------
static const char *kEldBabyHungryRub[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/hgy/eld_baby_hgy_rub1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/hgy/eld_baby_hgy_rub2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/hgy/eld_baby_hgy_rub3.png",
};

// ---------------------------
// Eldritch baby ANGRY wave (4-frame)
// ---------------------------
static const char *kEldBabyAngryWave[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/ang/eld_baby_agy_wave1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/ang/eld_baby_agy_wave2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/ang/eld_baby_agy_wave3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/ang/eld_baby_agy_wave4.png",
};

// ---------------------------
// Eldritch baby BORED ball (5-frame)
// ---------------------------
static const char *kEldBabyBoredBall[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/brd/eld_baby_brd_ball1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/brd/eld_baby_brd_ball2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/brd/eld_baby_brd_ball3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/brd/eld_baby_brd_ball4.png",
    "/raising_hell/graphics/pet/anim/eld/bb/brd/eld_baby_brd_ball5.png",
};

// ---------------------------
// Eldritch baby SICK bob (3-frame)
// ---------------------------
static const char *kEldBabySickBob[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/sck/eld_baby_sck_bob1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sck/eld_baby_sck_bob2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sck/eld_baby_sck_bob3.png",
};

// ---------------------------
// Eldritch baby SLEEPY yawn (4-frame)
// ---------------------------
static const char *kEldBabySleepyYawn[] = {
    "/raising_hell/graphics/pet/anim/eld/bb/slp/eld_baby_slp_yawn1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/slp/eld_baby_slp_yawn2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/slp/eld_baby_slp_yawn3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/slp/eld_baby_slp_yawn4.png",
};

// ---------------------------
// Eldritch teen HAPPY thumb (3-frame)
// ---------------------------
static const char *kEldTeenHappyBob[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_thumb1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_thumb2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_thumb3.png",
};

// ---------------------------
// Eldritch teen HUNGRY bite (6-frame)
// ---------------------------
static const char *kEldTeenHungryBite[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite4.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite5.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hgy/eld_tn_hgy_bite6.png",
};

// ---------------------------
// Eldritch teen ANGRY pose (4-frame)
// ---------------------------
static const char *kEldTeenAngryPose[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/agy/eld_tn_agy_pose1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/agy/eld_tn_agy_pose2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/agy/eld_tn_agy_pose3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/agy/eld_tn_agy_pose4.png",
};

// ---------------------------
// Eldritch teen BORED dribble (4-frame)
// ---------------------------
static const char *kEldTeenBoredDrib[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/brd/eld_tn_brd_drib1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/brd/eld_tn_brd_drib2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/brd/eld_tn_brd_drib3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/brd/eld_tn_brd_drib4.png",
};

// ---------------------------
// Eldritch teen SICK sneeze (5-frame)
// ---------------------------
static const char *kEldTeenSickSneeze[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/sck/eld_tn_sck_sneeze1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sck/eld_tn_sck_sneeze2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sck/eld_tn_sck_sneeze3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sck/eld_tn_sck_sneeze4.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sck/eld_tn_sck_sneeze5.png",
};

// ---------------------------
// Eldritch teen TIRED nod (4-frame)
// ---------------------------
static const char *kEldTeenTiredNod[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/trd/eld_tn_trd_nod1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/trd/eld_tn_trd_nod2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/trd/eld_tn_trd_nod3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/trd/eld_tn_trd_nod4.png",
};

// ---------------------------
// Eldritch adult ANGRY flex (4-frame)
// ---------------------------
static const char *kEldAdultAngryFlex[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/agy/eld_ad_agy_flex1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/agy/eld_ad_agy_flex2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/agy/eld_ad_agy_flex3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/agy/eld_ad_agy_flex4.png",
};

// ---------------------------
// Eldritch adult BORED spin (3-frame)
// ---------------------------
static const char *kEldAdultBoredSpin[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/brd/eld_ad_brd_spin1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/brd/eld_ad_brd_spin2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/brd/eld_ad_brd_spin3.png",
};

// ---------------------------
// Eldritch adult HUNGRY shake (4-frame)
// ---------------------------
static const char *kEldAdultHungryShake[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/hgy/eld_ad_hgy_shake1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hgy/eld_ad_hgy_shake2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hgy/eld_ad_hgy_shake3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hgy/eld_ad_hgy_shake4.png",
};

// ---------------------------
// Eldritch adult HAPPY spin (4-frame)
// ---------------------------
static const char *kEldAdultHappySpin[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/hpy/eld_ad_hpy_spin1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hpy/eld_ad_hpy_spin2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hpy/eld_ad_hpy_spin3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/hpy/eld_ad_hpy_spin4.png",
};

// ---------------------------
// Eldritch adult SICK hunch (4-frame)
// ---------------------------
static const char *kEldAdultSickHunch[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/sck/eld_ad_sck_hunch1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sck/eld_ad_sck_hunch2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sck/eld_ad_sck_hunch3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sck/eld_ad_sck_hunch4.png",
};

// ---------------------------
// Eldritch adult SLEEPY drink (4-frame)
// ---------------------------
static const char *kEldAdultSleepyDrink[] = {
    "/raising_hell/graphics/pet/anim/eld/ad/slp/eld_ad_slp_drink1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/slp/eld_ad_slp_drink2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/slp/eld_ad_slp_drink3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/slp/eld_ad_slp_drink4.png",
};

// ---------------------------
// Eldritch elder HAPPY pass (4-frame)
// ---------------------------
static const char *kEldElderHappyPass[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/hpy/eld_ed_hpy_pass1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hpy/eld_ed_hpy_pass2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hpy/eld_ed_hpy_pass3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hpy/eld_ed_hpy_pass4.png",
};

// ---------------------------
// Eldritch elder HUNGRY eat (4-frame)
// ---------------------------
static const char *kEldElderHungryEat[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/hgy/eld_ed_hgy_eat1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hgy/eld_ed_hgy_eat2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hgy/eld_ed_hgy_eat3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/hgy/eld_ed_hgy_eat4.png",
};

// ---------------------------
// Eldritch elder ANGRY shake (2-frame)
// ---------------------------
static const char *kEldElderAngryShake[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/agy/eld_ed_agy_shake1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/agy/eld_ed_agy_shake2.png",
};

// ---------------------------
// Eldritch elder BORED yoyo (4-frame)
// ---------------------------
static const char *kEldElderBoredYoyo[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/brd/eld_ed_brd_yoyo1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/brd/eld_ed_brd_yoyo2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/brd/eld_ed_brd_yoyo3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/brd/eld_ed_brd_yoyo4.png",
};

// ---------------------------
// Eldritch elder SICK sneeze (6-frame)
// ---------------------------
static const char *kEldElderSickSneeze[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze4.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze5.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sck/eld_ed_sck_sneeze6.png",
};

// ---------------------------
// Eldritch elder SLEEPY hold (4-frame)
// ---------------------------
static const char *kEldElderSleepyHold[] = {
    "/raising_hell/graphics/pet/anim/eld/ed/slp/eld_ed_slp_hold1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/slp/eld_ed_slp_hold2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/slp/eld_ed_slp_hold3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/slp/eld_ed_slp_hold4.png",
};

// ---------------------------
// Alien baby HAPPY
// ---------------------------
static const char *kAlienBabyHappy[] = {
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_turn1.png",
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_turn2.png",
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_turn3.png",
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_turn4.png",
};

static const char *kAlienBabyHappyGun[] = {
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_gun1.png",
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_gun2.png",
};

static const char *kAlienBabyHappyGunLeft[] = {
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_left_gun1.png",
    "/raising_hell/graphics/pet/anim/al/bb/hpy/al_bb_hpy_left_gun2.png",
};

static const char *kAlienBabyHungryStand[] = {
    "/raising_hell/graphics/pet/anim/al/bb/hgy/al_bb_hgy_stand1.png",
    "/raising_hell/graphics/pet/anim/al/bb/hgy/al_bb_hgy_stand2.png",
};

static const char *kAlienBabySickMoan[] = {
    "/raising_hell/graphics/pet/anim/al/bb/sck/al_bb_sck_moan1.png",
    "/raising_hell/graphics/pet/anim/al/bb/sck/al_bb_sck_moan2.png",
};

static const char *kAlienBabySickSneeze[] = {
    "/raising_hell/graphics/pet/anim/al/bb/sck/al_bb_sck_sneeze1.png",
    "/raising_hell/graphics/pet/anim/al/bb/sck/al_bb_sck_sneeze2.png",
    "/raising_hell/graphics/pet/anim/al/bb/sck/al_bb_sck_sneeze3.png",
};

static const char *kAlienBabyBoredIdle[] = {
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_blink1.png",
};

static const char *kAlienBabyBoredBlink[] = {
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_blink2.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_blink1.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_blink2.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_blink1.png",
};

static const char *kAlienBabyAngryStomp[] = {
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_stomp1.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_stomp2.png",
};

static const char *kAlienBabyAngryPounce[] = {
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce1.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce3.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce2.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce3.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce2.png",
    "/raising_hell/graphics/pet/anim/al/bb/agy/al_bb_agy_pounce1.png",
};

static const char *kAlienBabyTiredLay[] = {
    "/raising_hell/graphics/pet/anim/al/bb/trd/al_bb_trd_lay1.png",
    "/raising_hell/graphics/pet/anim/al/bb/trd/al_bb_trd_lay2.png",
};

static const char *kAlienBabyTiredHold[] = {
    "/raising_hell/graphics/pet/anim/al/bb/trd/al_bb_trd_lay3.png",
};

static const char *kAlienTeenHappySway[] = {
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_sway1.png",
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_sway2.png",
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_sway3.png",
};

static const char *kAlienTeenHappyFlick[] = {
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_flick1.png",
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_flick2.png",
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_flick3.png",
    "/raising_hell/graphics/pet/anim/al/tn/hpy/al_tn_hpy_flick4.png",
};

static const char *kAlienTeenBoredStomp[] = {
    "/raising_hell/graphics/pet/anim/al/tn/brd/al_tn_brd_stomp1.png",
    "/raising_hell/graphics/pet/anim/al/tn/brd/al_tn_brd_stomp2.png",
    "/raising_hell/graphics/pet/anim/al/tn/brd/al_tn_brd_stomp3.png",
};

static const char *kAlienTeenBoredFly[] = {
    "/raising_hell/graphics/pet/anim/al/tn/brd/al_tn_brd_fly1.png",
    "/raising_hell/graphics/pet/anim/al/tn/brd/al_tn_brd_fly2.png",
};

static const char *kAlienTeenAngryJump[] = {
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_jump1.png",
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_jump2.png",
};

static const char *kAlienTeenAngrySaber[] = {
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_saber1.png",
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_saber2.png",
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_saber3.png",
    "/raising_hell/graphics/pet/anim/al/tn/agy/al_tn_agy_saber4.png",
};

static const char *kAlienTeenHungryFork[] = {
    "/raising_hell/graphics/pet/anim/al/tn/hgy/al_tn_hgy_fork1.png",
    "/raising_hell/graphics/pet/anim/al/tn/hgy/al_tn_hgy_fork2.png",
};

static const char *kAlienTeenTiredSnore[] = {
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore1.png",
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore2.png",
};

static const char *kAlienTeenTiredBlink[] = {
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore1.png",
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore3.png",
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore2.png",
    "/raising_hell/graphics/pet/anim/al/tn/trd/al_tn_trd_snore3.png",
};

static const char *kAlienTeenSickLoop[] = {
    "/raising_hell/graphics/pet/anim/al/tn/sck/al_tn_sick1.png",
    "/raising_hell/graphics/pet/anim/al/tn/sck/al_tn_sick2.png",
    "/raising_hell/graphics/pet/anim/al/tn/sck/al_tn_sick3.png",
};

static const char *kAlienAdultHappyStance[] = {
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_stance1.png",
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_stance2.png",
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_stance3.png",
};

static const char *kAlienAdultHappyFlick[] = {
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_flick1.png",
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_flick2.png",
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_flick3.png",
    "/raising_hell/graphics/pet/anim/al/ad/hpy/aln_ad_hpy_flick2.png",
};

static const char *kAlienAdultBoredSigh[] = {
    "/raising_hell/graphics/pet/anim/al/ad/brd/aln_ad_brd_sigh1.png",
    "/raising_hell/graphics/pet/anim/al/ad/brd/aln_ad_brd_sigh2.png",
    "/raising_hell/graphics/pet/anim/al/ad/brd/aln_ad_brd_sigh3.png",
    "/raising_hell/graphics/pet/anim/al/ad/brd/aln_ad_brd_sigh2.png",
};

static const char *kAlienAdultAngryShoot[] = {
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot1.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot2.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot3.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot4.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot3.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot4.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot3.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot4.png",
    "/raising_hell/graphics/pet/anim/al/ad/agy/aln_ad_agy_shoot2.png",
};

static const char *kAlienAdultHungryBend[] = {
    "/raising_hell/graphics/pet/anim/al/ad/hgy/aln_ad_hgy_bend1.png",
    "/raising_hell/graphics/pet/anim/al/ad/hgy/aln_ad_hgy_bend2.png",
};

static const char *kAlienAdultTiredNod[] = {
    "/raising_hell/graphics/pet/anim/al/ad/trd/aln_ad_trd_nod1.png",
    "/raising_hell/graphics/pet/anim/al/ad/trd/aln_ad_trd_nod2.png",
    "/raising_hell/graphics/pet/anim/al/ad/trd/aln_ad_trd_nod3.png",
    "/raising_hell/graphics/pet/anim/al/ad/trd/aln_ad_trd_nod2.png",
};

static const char *kAlienAdultSickHeave[] = {
    "/raising_hell/graphics/pet/anim/al/ad/sck/aln_ad_sck_heave1.png",
    "/raising_hell/graphics/pet/anim/al/ad/sck/aln_ad_sck_heave2.png",
};

static const char *kAlienElderHappySmile[] = {
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_smile1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_smile2.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_smile3.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_smile2.png",
};

static const char *kAlienElderHappyCan[] = {
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can2.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can2.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_can2.png",
};

static const char *kAlienElderHappyCanLeft[] = {
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft2.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft2.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft1.png",
    "/raising_hell/graphics/pet/anim/al/ed/hpy/al_ed_hpy_canleft2.png",
};

static const char *kAlienElderBoredBlink[] = {
    "/raising_hell/graphics/pet/anim/al/ed/brd/al_ed_brd_blink1.png",
    "/raising_hell/graphics/pet/anim/al/ed/brd/al_ed_brd_blink2.png",
    "/raising_hell/graphics/pet/anim/al/ed/brd/al_ed_brd_blink3.png",
    "/raising_hell/graphics/pet/anim/al/ed/brd/al_ed_brd_blink4.png",
};

// ---------------------------
// Clip table
// ---------------------------
static const AnimClip kClips[] = {
    {ANIM_DEV_BABY_BORED_ROLL, kDevBabyBoredRoll, 4, 220, true},
    {ANIM_DEV_BABY_HUNGRY_RUB, kDevBabyHungryRub, 2, 220, true},
    {ANIM_DEV_BABY_ANGRY_BOB, kDevBabyAngryBob, 4, 220, true},
    {ANIM_DEV_BABY_ANGRY_SWIPE, kDevBabyAngrySwipe, 4, 90, false},
    {ANIM_DEV_BABY_SLEEPY_NOD, kDevBabySleepyNod, 3, 220, true},
    {ANIM_DEV_BABY_SICK_CRAWL, kDevBabySickCrawl, 3, 220, true},
    {ANIM_DEV_BABY_HAPPY_BALL, kDevBabyHappyBall, 4, 220, true},

    {ANIM_DEV_TEEN_HAPPY_POSE, kDevTeenHappyPose, 3, 220, true},
    {ANIM_DEV_TEEN_HUNGRY_RUB, kDevTeenHungryRub, 2, 220, true},
    {ANIM_DEV_TEEN_ANGRY_CLAW, kDevTeenAngryClaw, 4, 120, true},
    {ANIM_DEV_TEEN_SLEEPY_BOB, kDevTeenSleepyBob, 4, 260, true},
    {ANIM_DEV_TEEN_SICK_BOB, kDevTeenSickBob, 4, 220, true},
    {ANIM_DEV_TEEN_BORED_PLAY, kDevTeenBoredPlay, 4, 220, true},

    {ANIM_DEV_ADULT_HAPPY_TAIL, kDevAdultHappyTail, 4, 220, true},
    {ANIM_DEV_ADULT_HUNGRY_BEND, kDevAdultHungryBend, 4, 220, true},
    {ANIM_DEV_ADULT_ANGRY_STANCE, kDevAdultAngryStance, 2, 180, true},
    {ANIM_DEV_ADULT_ANGRY_KICK, kDevAdultAngryKick, 4, 110, true},
    {ANIM_DEV_ADULT_BORED_GAMEBOY, kDevAdultBoredGameboy, 3, 220, true},
    {ANIM_DEV_ADULT_TIRED_CHAIR_IDLE, kDevAdultTiredChairIdle, 1, 1000, true},
    {ANIM_DEV_ADULT_TIRED_CHAIR_BLINK, kDevAdultTiredChairBlink, 2, 120, false},
    {ANIM_DEV_ADULT_SICK_LAY, kDevAdultSickLay, 4, 240, true},

    {ANIM_DEV_ELDER_BORED_SPIN, kDevElderBoredSpin, 8, 140, true},
    {ANIM_DEV_ELDER_ANGRY_FIRE, kDevElderAngryFire, 4, 90, true},
    {ANIM_DEV_ELDER_HAPPY_SHAKE, kDevElderHappyShake, 4, 140, true},
    {ANIM_DEV_ELDER_HUNGRY_RUB, kDevElderHungryRub, 3, 180, true},
    {ANIM_DEV_ELDER_TIRED_SIT, kDevElderTiredSit, 2, 700, true},
    {ANIM_DEV_ELDER_SICK_COUGH, kDevElderSickCough, 3, 220, true},

    {ANIM_ELD_BABY_HAPPY_SIT, kEldBabyHappySit, 4, 220, true},
    {ANIM_ELD_BABY_HUNGRY_RUB, kEldBabyHungryRub, 3, 220, true},
    {ANIM_ELD_BABY_ANGRY_WAVE, kEldBabyAngryWave, 4, 180, true},
    {ANIM_ELD_BABY_BORED_BALL, kEldBabyBoredBall, 5, 220, true},
    {ANIM_ELD_BABY_SLEEPY_YAWN, kEldBabySleepyYawn, 4, 260, true},
    {ANIM_ELD_BABY_SICK_BOB, kEldBabySickBob, 3, 220, true},

    {ANIM_ELD_TEEN_HAPPY_THUMB, kEldTeenHappyBob, 3, 220, true},
    {ANIM_ELD_TEEN_HUNGRY_BITE, kEldTeenHungryBite, 6, 180, true},
    {ANIM_ELD_TEEN_ANGRY_POSE, kEldTeenAngryPose, 4, 180, true},
    {ANIM_ELD_TEEN_BORED_DRIB, kEldTeenBoredDrib, 4, 220, true},
    {ANIM_ELD_TEEN_SICK_SNEEZE, kEldTeenSickSneeze, 5, 140, true},
    {ANIM_ELD_TEEN_TIRED_NOD, kEldTeenTiredNod, 4, 260, true},

    {ANIM_ELD_ADULT_ANGRY_FLEX, kEldAdultAngryFlex, 4, 140, true},
    {ANIM_ELD_ADULT_BORED_SPIN, kEldAdultBoredSpin, 3, 200, true},
    {ANIM_ELD_ADULT_HUNGRY_SHAKE, kEldAdultHungryShake, 4, 160, true},
    {ANIM_ELD_ADULT_HAPPY_SPIN, kEldAdultHappySpin, 4, 160, true},
    {ANIM_ELD_ADULT_SICK_HUNCH, kEldAdultSickHunch, 4, 220, true},
    {ANIM_ELD_ADULT_SLEEPY_DRINK, kEldAdultSleepyDrink, 4, 240, true},

    {ANIM_ELD_ELDER_HAPPY_PASS, kEldElderHappyPass, 4, 240, true},
    {ANIM_ELD_ELDER_HUNGRY_EAT, kEldElderHungryEat, 4, 110, true},
    {ANIM_ELD_ELDER_ANGRY_SHAKE, kEldElderAngryShake, 2, 240, true},
    {ANIM_ELD_ELDER_BORED_YOYO, kEldElderBoredYoyo, 4, 110, true},
    {ANIM_ELD_ELDER_SICK_SNEEZE, kEldElderSickSneeze, 6, 90, true},
    {ANIM_ELD_ELDER_SLEEPY_HOLD, kEldElderSleepyHold, 4, 130, true},

    {ANIM_ALIEN_BABY_HAPPY, kAlienBabyHappy, 4, 220, true},
    {ANIM_ALIEN_BABY_HAPPY_GUN, kAlienBabyHappyGun, 2, 220, true},
    {ANIM_ALIEN_BABY_HAPPY_GUN_LEFT, kAlienBabyHappyGunLeft, 2, 220, true},
    {ANIM_ALIEN_BABY_HUNGRY_STAND, kAlienBabyHungryStand, 2, 260, true},
    {ANIM_ALIEN_BABY_SICK_MOAN, kAlienBabySickMoan, 2, 420, true},
    {ANIM_ALIEN_BABY_SICK_SNEEZE, kAlienBabySickSneeze, 3, 120, false},
    {ANIM_ALIEN_BABY_BORED_IDLE, kAlienBabyBoredIdle, 1, 1000, true},
    {ANIM_ALIEN_BABY_BORED_BLINK, kAlienBabyBoredBlink, 4, 120, false},
    {ANIM_ALIEN_BABY_ANGRY_STOMP, kAlienBabyAngryStomp, 2, 140, true},
    {ANIM_ALIEN_BABY_ANGRY_POUNCE, kAlienBabyAngryPounce, 6, 90, false},
    {ANIM_ALIEN_BABY_TIRED_LAY, kAlienBabyTiredLay, 2, 850, true},
    {ANIM_ALIEN_BABY_TIRED_HOLD, kAlienBabyTiredHold, 1, 3500, false},

    {ANIM_ALIEN_TEEN_HAPPY_SWAY, kAlienTeenHappySway, 3, 220, true},
    {ANIM_ALIEN_TEEN_HAPPY_FLICK, kAlienTeenHappyFlick, 4, 120, false},

    {ANIM_ALIEN_TEEN_BORED_STOMP, kAlienTeenBoredStomp, 3, 1000, true},
    {ANIM_ALIEN_TEEN_BORED_FLY, kAlienTeenBoredFly, 2, 180, true},
    {ANIM_ALIEN_TEEN_ANGRY_JUMP, kAlienTeenAngryJump, 2, 160, true},
    {ANIM_ALIEN_TEEN_ANGRY_SABER, kAlienTeenAngrySaber, 4, 120, false},

    {ANIM_ALIEN_TEEN_HUNGRY_FORK, kAlienTeenHungryFork, 2, 180, true},
    {ANIM_ALIEN_TEEN_TIRED_SNORE, kAlienTeenTiredSnore, 2, 650, true},
    {ANIM_ALIEN_TEEN_TIRED_BLINK, kAlienTeenTiredBlink, 4, 140, false},
    {ANIM_ALIEN_TEEN_SICK_LOOP, kAlienTeenSickLoop, 3, 180, true},

    {ANIM_ALIEN_ADULT_HAPPY_STANCE, kAlienAdultHappyStance, 3, 220, true},
    {ANIM_ALIEN_ADULT_HAPPY_FLICK, kAlienAdultHappyFlick, 4, 120, false},
    {ANIM_ALIEN_ADULT_BORED_SIGH, kAlienAdultBoredSigh, 4, 220, true},
    {ANIM_ALIEN_ADULT_ANGRY_SHOOT, kAlienAdultAngryShoot, 9, 120, true},
    {ANIM_ALIEN_ADULT_HUNGRY_BEND, kAlienAdultHungryBend, 2, 1000, true},
    {ANIM_ALIEN_ADULT_TIRED_NOD, kAlienAdultTiredNod, 4, 260, true},
    {ANIM_ALIEN_ADULT_SICK_HEAVE, kAlienAdultSickHeave, 2, 320, true},

    {ANIM_ALIEN_ELDER_HAPPY_SMILE, kAlienElderHappySmile, 4, 500, true},
    {ANIM_ALIEN_ELDER_HAPPY_CAN, kAlienElderHappyCan, 6, 500, false},
    {ANIM_ALIEN_ELDER_HAPPY_CAN_LEFT, kAlienElderHappyCanLeft, 6, 500, false},
    {ANIM_ALIEN_ELDER_BORED_BLINK, kAlienElderBoredBlink, 4, 500, true},

};

const AnimClip *animGetClip(AnimId id)
{
  for (uint32_t i = 0; i < (sizeof(kClips) / sizeof(kClips[0])); i++)
  {
    if (kClips[i].id == id)
      return &kClips[i];
  }
  return nullptr;
}

// ---------------------------
// Behavior table (gesture overrides)
// ---------------------------
static const AnimBehavior kBehaviors[] = {
    // Devil baby angry: idle loops; swipe triggers occasionally.
    {ANIM_DEV_BABY_ANGRY_BOB, ANIM_DEV_BABY_ANGRY_SWIPE, 20UL * 1000UL, 35UL * 1000UL},

    // Devil adult angry: stance loops; kick triggers occasionally.
    {ANIM_DEV_ADULT_ANGRY_STANCE, ANIM_DEV_ADULT_ANGRY_KICK, 18UL * 1000UL, 28UL * 1000UL},

    // Devil adult tired chair: hold frame 1, occasionally blink (2->3) then return to 1.
    {ANIM_DEV_ADULT_TIRED_CHAIR_IDLE, ANIM_DEV_ADULT_TIRED_CHAIR_BLINK, 9UL * 1000UL, 11UL * 1000UL},

    // Alien baby bored: hold frame 1, occasionally blink.
    {ANIM_ALIEN_BABY_BORED_IDLE, ANIM_ALIEN_BABY_BORED_BLINK, 2500UL, 6500UL},

    // Alien baby angry: stomp loops; pounce triggers occasionally.
    {ANIM_ALIEN_BABY_ANGRY_STOMP, ANIM_ALIEN_BABY_ANGRY_POUNCE, 4500UL, 9000UL},

    // Alien baby tired: gentle lay loop; occasional longer exhausted hold.
    {ANIM_ALIEN_BABY_TIRED_LAY, ANIM_ALIEN_BABY_TIRED_HOLD, 5000UL, 12000UL},

    // Alien baby sick: moan loop; sneeze triggers occasionally.
    {ANIM_ALIEN_BABY_SICK_MOAN, ANIM_ALIEN_BABY_SICK_SNEEZE, 4500UL, 9000UL},

    // Alien teen happy: sway loops; hair/hand flick triggers occasionally.
    {ANIM_ALIEN_TEEN_HAPPY_SWAY, ANIM_ALIEN_TEEN_HAPPY_FLICK, 4500UL, 9000UL},

    // Alien adult happy: stance loops with a long frame-1 hold; hair flick triggers occasionally.
    {ANIM_ALIEN_ADULT_HAPPY_STANCE, ANIM_ALIEN_ADULT_HAPPY_FLICK, 6500UL, 13000UL},

    // Alien elder happy: smile loop; watering-can gesture triggers occasionally.
    // Direction is resolved at trigger time based on the pet's last wander facing.
    {ANIM_ALIEN_ELDER_HAPPY_SMILE, ANIM_ALIEN_ELDER_HAPPY_CAN, 6500UL, 13000UL},

    // Alien teen angry: jump loops; saber triggers occasionally.
    {ANIM_ALIEN_TEEN_ANGRY_JUMP, ANIM_ALIEN_TEEN_ANGRY_SABER, 4500UL, 9000UL},

    // Alien teen tired: snore loops; blink sequence triggers occasionally.
    {ANIM_ALIEN_TEEN_TIRED_SNORE, ANIM_ALIEN_TEEN_TIRED_BLINK, 6000UL, 12000UL},
};

const AnimBehavior *animGetBehavior(AnimId baseId)
{
  for (uint32_t i = 0; i < (sizeof(kBehaviors) / sizeof(kBehaviors[0])); i++)
  {
    if (kBehaviors[i].baseId == baseId)
      return &kBehaviors[i];
  }
  return nullptr;
}

// ---------------------------
// Selection (PET SCREEN only)
// Uses mood resolver + stage routing.
// ---------------------------

// Forward declaration
static AnimId devClipForMood(uint8_t stage, PetMood mood);

static AnimId devClipForMood(uint8_t stage, PetMood mood)
{
  // Stage 0 = baby
  if (stage == 0)
  {
    switch (mood)
    {
    case MOOD_SICK:
      return ANIM_DEV_BABY_SICK_CRAWL;
    case MOOD_TIRED:
      return ANIM_DEV_BABY_SLEEPY_NOD;
    case MOOD_HUNGRY:
      return ANIM_DEV_BABY_HUNGRY_RUB;
    case MOOD_MAD:
      return ANIM_DEV_BABY_ANGRY_BOB;
    case MOOD_BORED:
      return ANIM_DEV_BABY_BORED_ROLL;
    case MOOD_HAPPY:
      return ANIM_DEV_BABY_HAPPY_BALL;
    default:
      return ANIM_DEV_BABY_HAPPY_BALL;
    }
  }

  // Stage 1 = teen
  if (stage == 1)
  {
    switch (mood)
    {
    case MOOD_SICK:
      return ANIM_DEV_TEEN_SICK_BOB;
    case MOOD_TIRED:
      return ANIM_DEV_TEEN_SLEEPY_BOB;
    case MOOD_HUNGRY:
      return ANIM_DEV_TEEN_HUNGRY_RUB;
    case MOOD_MAD:
      return ANIM_DEV_TEEN_ANGRY_CLAW;
    case MOOD_BORED:
      return ANIM_DEV_TEEN_BORED_PLAY;
    case MOOD_HAPPY:
      return ANIM_DEV_TEEN_HAPPY_POSE;
    default:
      return ANIM_DEV_TEEN_HAPPY_POSE;
    }
  }

  // Stage 2 = adult
  if (stage == 2)
  {
    switch (mood)
    {
    case MOOD_MAD:
      return ANIM_DEV_ADULT_ANGRY_STANCE;
    case MOOD_HUNGRY:
      return ANIM_DEV_ADULT_HUNGRY_BEND;
    case MOOD_HAPPY:
      return ANIM_DEV_ADULT_HAPPY_TAIL;
    case MOOD_BORED:
      return ANIM_DEV_ADULT_BORED_GAMEBOY;
    case MOOD_TIRED:
      return ANIM_DEV_ADULT_TIRED_CHAIR_IDLE;
    case MOOD_SICK:
      return ANIM_DEV_ADULT_SICK_LAY;
    default:
      return ANIM_DEV_ADULT_HAPPY_TAIL;
    }
  }

  // Stage 3+ = elder
  switch (mood)
  {
  case MOOD_MAD:
    return ANIM_DEV_ELDER_ANGRY_FIRE;
  case MOOD_SICK:
    return ANIM_DEV_ELDER_SICK_COUGH;
  case MOOD_HAPPY:
    return ANIM_DEV_ELDER_HAPPY_SHAKE;
  case MOOD_HUNGRY:
    return ANIM_DEV_ELDER_HUNGRY_RUB;
  case MOOD_TIRED:
    return ANIM_DEV_ELDER_TIRED_SIT;
  case MOOD_BORED:
    return ANIM_DEV_ELDER_BORED_SPIN;
  default:
    return ANIM_DEV_ELDER_HAPPY_SHAKE;
  }
}

static AnimId eldBabyClipForMood(PetMood mood)
{
  switch (mood)
  {
  case MOOD_SICK:
    return ANIM_ELD_BABY_SICK_BOB;
  case MOOD_TIRED:
    return ANIM_ELD_BABY_SLEEPY_YAWN;
  case MOOD_HUNGRY:
    return ANIM_ELD_BABY_HUNGRY_RUB;
  case MOOD_MAD:
    return ANIM_ELD_BABY_ANGRY_WAVE;
  case MOOD_BORED:
    return ANIM_ELD_BABY_BORED_BALL;
  case MOOD_HAPPY:
    return ANIM_ELD_BABY_HAPPY_SIT;
  default:
    return ANIM_ELD_BABY_HAPPY_SIT;
  }
}

static AnimId eldTeenClipForMood(PetMood mood)
{
  switch (mood)
  {
  case MOOD_SICK:
    return ANIM_ELD_TEEN_SICK_SNEEZE;
  case MOOD_TIRED:
    return ANIM_ELD_TEEN_TIRED_NOD;
  case MOOD_HUNGRY:
    return ANIM_ELD_TEEN_HUNGRY_BITE;
  case MOOD_MAD:
    return ANIM_ELD_TEEN_ANGRY_POSE;
  case MOOD_BORED:
    return ANIM_ELD_TEEN_BORED_DRIB;
  case MOOD_HAPPY:
    return ANIM_ELD_TEEN_HAPPY_THUMB;
  default:
    return ANIM_ELD_TEEN_HAPPY_THUMB;
  }
}

static AnimId eldAdultClipForMood(PetMood mood)
{
  switch (mood)
  {
  case MOOD_SICK:
    return ANIM_ELD_ADULT_SICK_HUNCH;
  case MOOD_TIRED:
    return ANIM_ELD_ADULT_SLEEPY_DRINK;
  case MOOD_HUNGRY:
    return ANIM_ELD_ADULT_HUNGRY_SHAKE;
  case MOOD_MAD:
    return ANIM_ELD_ADULT_ANGRY_FLEX;
  case MOOD_BORED:
    return ANIM_ELD_ADULT_BORED_SPIN;
  case MOOD_HAPPY:
    return ANIM_ELD_ADULT_HAPPY_SPIN;
  default:
    return ANIM_ELD_ADULT_HAPPY_SPIN;
  }
}

static AnimId eldElderClipForMood(PetMood mood)
{
  switch (mood)
  {
  case MOOD_SICK:
    return ANIM_ELD_ELDER_SICK_SNEEZE;
  case MOOD_TIRED:
    return ANIM_ELD_ELDER_SLEEPY_HOLD;
  case MOOD_HUNGRY:
    return ANIM_ELD_ELDER_HUNGRY_EAT;
  case MOOD_MAD:
    return ANIM_ELD_ELDER_ANGRY_SHAKE;
  case MOOD_BORED:
    return ANIM_ELD_ELDER_BORED_YOYO;
  case MOOD_HAPPY:
    return ANIM_ELD_ELDER_HAPPY_PASS;
  default:
    return ANIM_ELD_ELDER_HAPPY_PASS;
  }
}

static AnimId alienBabyClipForMood(PetMood mood)
{
  if (mood == MOOD_MAD)
    return ANIM_ALIEN_BABY_ANGRY_STOMP;

  if (mood == MOOD_SICK)
    return ANIM_ALIEN_BABY_SICK_MOAN;

  if (mood == MOOD_HUNGRY)
    return ANIM_ALIEN_BABY_HUNGRY_STAND;

  if (mood == MOOD_TIRED)
    return ANIM_ALIEN_BABY_TIRED_LAY;

  if (mood == MOOD_BORED)
    return ANIM_ALIEN_BABY_BORED_IDLE;

  static bool happyChoiceGun = false;
  static unsigned long nextHappyChoiceAt = 0;

  if (mood != MOOD_HAPPY)
  {
    nextHappyChoiceAt = 0;
    return ANIM_ALIEN_BABY_HAPPY;
  }

  const unsigned long now = millis();

  if (nextHappyChoiceAt == 0 || now >= nextHappyChoiceAt)
  {
    happyChoiceGun = (random(2) == 0);
    nextHappyChoiceAt = now + random(4500UL, 9000UL);
  }

  if (!happyChoiceGun)
    return ANIM_ALIEN_BABY_HAPPY;

  return petPresentationFacingLeft() ? ANIM_ALIEN_BABY_HAPPY_GUN_LEFT : ANIM_ALIEN_BABY_HAPPY_GUN;
}

static AnimId alienClipForMood(uint8_t stage, PetMood mood)
{
  switch (stage)
  {
  case 0:
    return alienBabyClipForMood(mood);

  case 1:
    if (mood == MOOD_HAPPY)
      return ANIM_ALIEN_TEEN_HAPPY_SWAY;

    if (mood == MOOD_BORED)
      return ANIM_ALIEN_TEEN_BORED_STOMP;

    if (mood == MOOD_MAD)
      return ANIM_ALIEN_TEEN_ANGRY_JUMP;

    if (mood == MOOD_HUNGRY)
      return ANIM_ALIEN_TEEN_HUNGRY_FORK;

    if (mood == MOOD_TIRED)
      return ANIM_ALIEN_TEEN_TIRED_SNORE;

    if (mood == MOOD_SICK)
      return ANIM_ALIEN_TEEN_SICK_LOOP;

    return alienBabyClipForMood(mood);

  case 2:
    if (mood == MOOD_HAPPY)
      return ANIM_ALIEN_ADULT_HAPPY_STANCE;

    if (mood == MOOD_BORED)
      return ANIM_ALIEN_ADULT_BORED_SIGH;

    if (mood == MOOD_MAD)
      return ANIM_ALIEN_ADULT_ANGRY_SHOOT;

    if (mood == MOOD_HUNGRY)
      return ANIM_ALIEN_ADULT_HUNGRY_BEND;

    if (mood == MOOD_TIRED)
      return ANIM_ALIEN_ADULT_TIRED_NOD;

    if (mood == MOOD_SICK)
      return ANIM_ALIEN_ADULT_SICK_HEAVE;

    return alienBabyClipForMood(mood);

  case 3:
    if (mood == MOOD_HAPPY)
      return ANIM_ALIEN_ELDER_HAPPY_SMILE;

    if (mood == MOOD_BORED)
      return ANIM_ALIEN_ELDER_BORED_BLINK;

    return alienBabyClipForMood(mood);
  }
}

AnimId animSelectPetScreen()
{
  // Animate whenever the UI is showing a surface that still displays the pet.
  const bool petVisibleUi = (g_app.uiState == UIState::PET_SCREEN) || (g_app.uiState == UIState::CLOCK_MODE) ||
                            (g_app.uiState == UIState::SLEEP_MENU) || (g_app.uiState == UIState::INVENTORY) ||
                            (g_app.uiState == UIState::SHOP);

  if (!petVisibleUi)
    return ANIM_NONE;

  // Clock Mode should never use the dedicated sleeping clips.
  // Some pet/evo combinations do not have a valid sleeping sprite there,
  // so force the tired pose instead.
  const bool clockModeSleepingOverride = (g_app.uiState == UIState::CLOCK_MODE) && pet.isSleeping;

  // Devil uses its existing routing.
  if (pet.type == PET_DEVIL)
  {
    if (clockModeSleepingOverride)
      return devClipForMood(pet.evoStage, MOOD_TIRED);

    if (pet.isSleeping)
    {
      switch (pet.evoStage)
      {
      case 0:
        return ANIM_DEV_BABY_SLEEPY_NOD;
      case 1:
        return ANIM_DEV_TEEN_SLEEPY_BOB;
      case 2:
        return ANIM_DEV_ADULT_TIRED_CHAIR_IDLE;
      default:
        return ANIM_DEV_ELDER_TIRED_SIT;
      }
    }

    return devClipForMood(pet.evoStage, pet.getMood());
  }

  // Eldritch uses its full stage/mood routing.
  if (pet.type == PET_ELDRITCH)
  {
    if (clockModeSleepingOverride)
    {
      switch (pet.evoStage)
      {
      case 0:
        return eldBabyClipForMood(MOOD_TIRED);
      case 1:
        return eldTeenClipForMood(MOOD_TIRED);
      case 2:
        return eldAdultClipForMood(MOOD_TIRED);
      case 3:
      default:
        return eldElderClipForMood(MOOD_TIRED);
      }
    }

    if (pet.isSleeping)
    {
      switch (pet.evoStage)
      {
      case 0:
        return ANIM_ELD_BABY_SLEEPY_YAWN;
      case 1:
        return ANIM_ELD_TEEN_TIRED_NOD;
      case 2:
        return ANIM_ELD_ADULT_IDLE_1F;
      default:
        return ANIM_ELD_ELDER_IDLE_1F;
      }
    }

    switch (pet.evoStage)
    {
    case 0:
      return eldBabyClipForMood(pet.getMood());
    case 1:
      return eldTeenClipForMood(pet.getMood());
    case 2:
      return eldAdultClipForMood(pet.getMood());
    case 3:
    default:
      return eldElderClipForMood(pet.getMood());
    }
  }

  // Alien uses its own stage router.
  // For now, all Alien stages resolve to baby happy until more Alien clips exist.
  if (pet.type == PET_ALIEN)
  {
    if (clockModeSleepingOverride)
      return alienClipForMood(pet.evoStage, MOOD_TIRED);

    if (pet.isSleeping)
      return alienClipForMood(pet.evoStage, MOOD_TIRED);

    return alienClipForMood(pet.evoStage, pet.getMood());
  }

  return ANIM_NONE;
}