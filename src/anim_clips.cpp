#include "anim_clips.h"

#include "app_state.h"
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
// Devil teen HAPPY pose (your new 2-frame animation)
// ---------------------------
static const char *kDevTeenHappyPose[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/hpy/dev_tn_hpy_pose1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/hpy/dev_tn_hpy_pose2.png",
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
// Devil teen BORED play (3-frame)
// ---------------------------
static const char *kDevTeenBoredPlay[] = {
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_teen_brd_play1.png",
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_teen_brd_play2.png",
    "/raising_hell/graphics/pet/anim/dev/tn/brd/dev_teen_brd_play3.png",
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
// Eldritch teen HAPPY bob (4-frame)
// ---------------------------
static const char *kEldTeenHappyBob[] = {
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_bob1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_bob2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_bob3.png",
    "/raising_hell/graphics/pet/anim/eld/tn/hpy/eld_tn_hpy_bob4.png",
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

    {ANIM_DEV_TEEN_HAPPY_POSE, kDevTeenHappyPose, 2, 220, true},
    {ANIM_DEV_TEEN_HUNGRY_RUB, kDevTeenHungryRub, 2, 220, true},
    {ANIM_DEV_TEEN_ANGRY_CLAW, kDevTeenAngryClaw, 4, 120, true},
    {ANIM_DEV_TEEN_SLEEPY_BOB, kDevTeenSleepyBob, 4, 260, true},
    {ANIM_DEV_TEEN_SICK_BOB, kDevTeenSickBob, 4, 220, true},
    {ANIM_DEV_TEEN_BORED_PLAY, kDevTeenBoredPlay, 3, 220, true},

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

    {ANIM_ELD_TEEN_HAPPY_BOB, kEldTeenHappyBob, 4, 220, true},
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
    {ANIM_ELD_ELDER_ANGRY_SHAKE, kEldElderAngryShake, 2, 110, true},
    {ANIM_ELD_ELDER_BORED_YOYO, kEldElderBoredYoyo, 4, 110, true},
    {ANIM_ELD_ELDER_SICK_SNEEZE, kEldElderSickSneeze, 6, 90, false},
    {ANIM_ELD_ELDER_SLEEPY_HOLD, kEldElderSleepyHold, 4, 130, true},

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
    return ANIM_ELD_TEEN_HAPPY_BOB;
  default:
    return ANIM_ELD_TEEN_HAPPY_BOB;
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

AnimId animSelectPetScreen()
{
  // Animate whenever the UI is showing a "tab" surface that still displays the pet.
  // (Stats/Feed/Play are also PET_SCREEN with different currentTab.)
  const bool petVisibleUi = (g_app.uiState == UIState::PET_SCREEN) || (g_app.uiState == UIState::SLEEP_MENU) ||
                            (g_app.uiState == UIState::INVENTORY) || (g_app.uiState == UIState::SHOP);

  if (!petVisibleUi)
    return ANIM_NONE;

  // Devil uses its existing routing.
  if (pet.type == PET_DEVIL)
  {
    // Keep returning a loop even while sleeping so it doesn't "freeze" after long idle.
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

  // Eldritch routing (baby + teen clips, placeholders for later stages).
  if (pet.type == PET_ELDRITCH)
  {
    // While sleeping, force a loop that still animates.
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
      return eldElderClipForMood(pet.getMood());

    default:
      return ANIM_ELD_ELDER_IDLE_1F;
    }
  }

  return ANIM_NONE;
}