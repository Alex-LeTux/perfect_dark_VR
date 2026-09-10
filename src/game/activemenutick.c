#include <ultra64.h>
#include "constants.h"
#include "game/activemenu.h"
#include "game/pdmode.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/options.h"
#include "bss.h"
#include "lib/joy.h"
#include "lib/str.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64
#include "game/player.h"
#include "input.h"
#endif

// -- ADDITION FOR THE VR POINTER --
extern bool vr_get_menu_pointer_coords(f32 *out_x, f32 *out_y, bool is_first_frame);
extern int vr_init_done;
// -------------------------------

void amTick(void)
{
	s32 prevplayernum = g_Vars.currentplayernum;
	s32 i;

// --- VR ADDITION: Detecting when the menu is opened ---
    static s32 prev_menumode = AMMODE_CLOSED;
    bool just_opened = false;
    // ---------------------------------------------------

	for (i = 0; i < PLAYERCOUNT(); i++) {
		setCurrentPlayerNum(i);
		g_AmIndex = g_Vars.currentplayernum;

        // --- VR ADDITION: Update the state only for the local player ---
        if (i == 0) {
            s32 current_mode = g_Vars.currentplayer->activemenumode;
            if (current_mode != AMMODE_CLOSED && prev_menumode == AMMODE_CLOSED) {
                just_opened = true;
            }
            prev_menumode = current_mode;
        }
        // ------------------------------------------------------------------

		if (g_AmMenus[g_AmIndex].togglefunc) {
			if (bgunConsiderToggleGunFunction(60, false, true, 0) > 0) {
				g_AmMenus[g_AmIndex].togglefunc = false;
			}
		} else {
			// empty
		}

		if (g_Vars.normmplayerisrunning == false
				&& invGetCount() != g_AmMenus[g_AmIndex].numitems) {
			amAssignWeaponSlots();
		}

		if (g_Vars.currentplayer->activemenumode != AMMODE_CLOSED) {
			s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);
			s8 contpadnum = optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
			s32 numsamples = joyGetNumSamples();
			s32 j;
			u32 amask, lrtmask, umask, dmask, lmask, rmask;

			if (controlmode == CONTROLMODE_PC) {
				amask = D_JPAD;
				lrtmask = R_TRIG;
				umask = U_CBUTTONS;
				dmask = D_CBUTTONS;
				lmask = L_CBUTTONS;
				rmask = R_CBUTTONS;
			} else {
				amask = A_BUTTON;
				lrtmask = L_TRIG | R_TRIG;
				umask = U_JPAD | U_CBUTTONS;
				dmask = D_JPAD | D_CBUTTONS;
				lmask = L_JPAD | L_CBUTTONS;
				rmask = R_JPAD | R_CBUTTONS;
			}

			for (j = 0; j < numsamples; j++) {
				s8 gotonextscreen = false;
				s8 cstickx = joyGetStickXOnSample(j, contpadnum);
				s8 csticky = joyGetStickYOnSample(j, contpadnum);
#ifdef AVOID_UB
				// if cstickx is -128, it will get negated and stored into absstickx, negating it again if it's 8 bit
				s32 absstickx;
				s32 abssticky;
#else
				s8 absstickx;
				s8 abssticky;
#endif
				u32 buttonsstate = joyGetButtonsOnSample(j, contpadnum, 0xffffffff);
				u32 buttonspressed = joyGetButtonsPressedOnSample(j, contpadnum, 0xffffffff);
				bool stickpushed = false;
				s32 slotnum;
				bool stayopen;
				bool toggle;
				s32 row;
				s32 column;

				column = 1;
				row = 1;
				stayopen = false;
				toggle = false;

				g_AmMenus[g_AmIndex].allbots = false;

#ifndef PLATFORM_N64
                s32 newstickx = (s32)cstickx;
                s32 newsticky = (s32)csticky;

                if (j == 0 && g_Vars.currentplayernum == 0) {
                    if (vr_init_done) {
                        f32 pointer_x = 0.0f;
                        f32 pointer_y = 0.0f;

                        if (vr_get_menu_pointer_coords(&pointer_x, &pointer_y, just_opened)) {
                            newstickx = (s32)pointer_x;
                            newsticky = -(s32)pointer_y;
                        }
                    }
                    else if (inputMouseIsLocked()) {
                        f32 mdx, mdy;
                        struct activemenu *am = &g_AmMenus[g_AmIndex];
                        inputMouseGetAbsScaledDelta(&mdx, &mdy);
                        if (mdx || mdy) {
                            am->mousex += mdx * PLAYER_EXTCFG().radialmenuspeed;
                            am->mousey += mdy * PLAYER_EXTCFG().radialmenuspeed;
                            am->mousex = (am->mousex > 127.f) ? 127.f : (am->mousex < -128.f) ? -128.f : am->mousex;
                            am->mousey = (am->mousey > 127.f) ? 127.f : (am->mousey < -128.f) ? -128.f : am->mousey;
                        }
                        newstickx += (s32)am->mousex;
                        newsticky -= (s32)am->mousey;
                    }
                }

                cstickx = (newstickx < -128) ? -128 : (newstickx > 127) ? 127 : newstickx;
                csticky = (newsticky < -128) ? -128 : (newsticky > 127) ? 127 : newsticky;
#endif

				if (g_Vars.currentplayer->activemenumode == AMMODE_EDIT) {
					buttonsstate = buttonsstate & amask;
					cstickx = 0;
					csticky = 0;
					buttonspressed = 0;
				}

				// JPN fixes the bug that's documented in amChangeScreen
				if (controlmode == CONTROLMODE_13 || controlmode == CONTROLMODE_14) {
					if (buttonsstate & (L_TRIG | R_TRIG)) {
						stayopen = true;
					}

					if (buttonsstate & A_BUTTON) {
#if VERSION >= VERSION_JPN_FINAL || !defined(PLATFORM_N64)
						if (g_Vars.currentplayer->numaibuddies > 0) {
							g_AmMenus[g_AmIndex].allbots = true;
						}
#else
						g_AmMenus[g_AmIndex].allbots = true;
#endif
					}
				} else {
					if (buttonsstate & amask) {
						stayopen = true;
					}

					if (buttonsstate & lrtmask) {
#if VERSION >= VERSION_JPN_FINAL || !defined(PLATFORM_N64)
						if (g_Vars.currentplayer->numaibuddies > 0) {
							g_AmMenus[g_AmIndex].allbots = true;
						}
#else
						g_AmMenus[g_AmIndex].allbots = true;
#endif
					}
				}

				// If entering allbots mode, save current screen
				if (g_AmMenus[g_AmIndex].allbots
						&& g_AmMenus[g_AmIndex].screenindex >= 2
						&& g_AmMenus[g_AmIndex].origscreennum == 0) {
					g_AmMenus[g_AmIndex].origscreennum = g_AmMenus[g_AmIndex].screenindex;
					g_AmMenus[g_AmIndex].screenindex = 2;
					amChangeScreen(0);
				}

				// If exiting allbots mode, return to original screen
				if (!g_AmMenus[g_AmIndex].allbots
						&& g_AmMenus[g_AmIndex].origscreennum) {
					g_AmMenus[g_AmIndex].screenindex = g_AmMenus[g_AmIndex].origscreennum;
					g_AmMenus[g_AmIndex].origscreennum = 0;
					amChangeScreen(0);
				}

				if (buttonsstate & umask) {
					row = 0;
				}

				if (buttonsstate & dmask) {
					row = 2;
				}

				if (buttonsstate & lmask) {
					column = 0;
				}

				if (buttonsstate & rmask) {
					column = 2;
				}

				if (controlmode == CONTROLMODE_23
						|| controlmode == CONTROLMODE_24
						|| controlmode == CONTROLMODE_22
						|| controlmode == CONTROLMODE_21) {
					s8 contpadnum2 = optionsGetContpadNum2(g_Vars.currentplayerstats->mpindex);
					s8 cstickx2 = joyGetStickXOnSample(j, contpadnum2);
					s8 csticky2 = joyGetStickYOnSample(j, contpadnum2);
					u32 buttonsstate2 = joyGetButtonsOnSample(j, contpadnum2, 0xffffffff);
					u32 buttonspressed2 = joyGetButtonsPressedOnSample(j, contpadnum2, 0xffffffff);

					if (g_Vars.currentplayer->activemenumode == AMMODE_EDIT) {
						buttonsstate2 = buttonsstate2 & A_BUTTON;
						cstickx2 = 0;
						csticky2 = 0;
						buttonspressed2 = 0;
					}

					if (buttonsstate2 & A_BUTTON) {
						stayopen = true;
					}

					if (buttonspressed2 & Z_TRIG) {
						toggle = true;
					}

					if (buttonsstate2 & umask) {
						row = 0;
					}

					if (buttonsstate2 & dmask) {
						row = 2;
					}

					if (buttonsstate2 & lmask) {
						column = 0;
					}

					if (buttonsstate2 & rmask) {
						column = 2;
					}

					absstickx = cstickx2 < 0 ? -cstickx2 : cstickx2;
					abssticky = csticky2 < 0 ? -csticky2 : csticky2;

					if (absstickx > 20 || abssticky > 20) {
						if ((f32)abssticky / (f32)absstickx < 0.268f) {
							row = 1;
							column = cstickx2 < 0 ? 0 : 2;
						} else if ((f32)absstickx / (f32)abssticky < 0.268f) {
							column = 1;
							row = csticky2 < 0 ? 2 : 0;
						} else {
							column = cstickx2 < 0 ? 0 : 2;
							row = csticky2 < 0 ? 2 : 0;
						}

						stickpushed = true;
					}
				}

				absstickx = cstickx < 0 ? -cstickx : cstickx;
				abssticky = csticky < 0 ? -csticky : csticky;

				if (absstickx > 20 || abssticky > 20) {
					stickpushed = true;

					if ((f32)abssticky / (f32)absstickx < 0.268f) {
						column = cstickx < 0 ? 0 : 2;
						row = 1;
					} else if ((f32)absstickx / (f32)abssticky < 0.268f) {
						column = 1;
						row = csticky < 0 ? 2 : 0;
					} else {
						column = cstickx < 0 ? 0 : 2;
						row = csticky < 0 ? 2 : 0;
					}
				}

				if (g_Vars.currentplayer->isdead) {
					stayopen = false;
				}

#if (VERSION >= VERSION_NTSC_1_0) && defined(PLATFORM_N64)
				if (g_Vars.lvupdate240 == 0) {
					stayopen = false;
				}
#endif

				if (!stayopen &&
						(g_Vars.currentplayer->activemenumode != AMMODE_EDIT || g_Menus[g_MpPlayerNum].curdialog == NULL)) {
					amClose();
					break;
				}

				if (buttonspressed & Z_TRIG) {
					toggle = true;
				}

				if (toggle) {
					if (g_AmMenus[g_AmIndex].screenindex >= 2) {
						if (g_Vars.numaibuddies && g_MissionConfig.iscoop) {
							// Bot command screen, in coop with AI buddies
							if (g_AmMenus[g_AmIndex].slotnum == 4) {
								gotonextscreen = true;
							} else {
								amApply(g_AmMenus[g_AmIndex].slotnum);
							}
						} else {
							// Bot command screen, in multiplayer
							if (g_AmBotCommands[g_AmMenus[g_AmIndex].slotnum] == AIBOTCMD_ATTACK) {
								amOpenPickTarget();
							} else if (g_AmMenus[g_AmIndex].allbots == false) {
								gotonextscreen = true;
#if VERSION < VERSION_NTSC_1_0
								if (g_AmMenus[g_AmIndex].slotnum != 4) {
									amApply(g_AmMenus[g_AmIndex].slotnum);
								}
#endif
							}

#if VERSION >= VERSION_NTSC_1_0
							if (g_AmMenus[g_AmIndex].slotnum != 4) {
								amApply(g_AmMenus[g_AmIndex].slotnum);
							}
#endif
						}
					} else {
						// Weapon or function screen
						if (g_AmMenus[g_AmIndex].slotnum == 4) {
							gotonextscreen = true;
						} else {
							amApply(g_AmMenus[g_AmIndex].slotnum);
						}
					}
				}

				if (gotonextscreen) {
					amChangeScreen(gotonextscreen);

					// If weapon has no functions, skip past function screen
					if (g_AmMenus[g_AmIndex].screenindex == 1) {
						struct weaponfunc *pri = weaponGetFunction(&g_Vars.currentplayer->hands[0].gset, FUNC_PRIMARY);
						struct weaponfunc *sec = weaponGetFunction(&g_Vars.currentplayer->hands[0].gset, FUNC_SECONDARY);

						if (!pri && !sec) {
							amChangeScreen(gotonextscreen);
						}
					}
				}

				slotnum = column * 1 + row * 3;

				if (g_Vars.currentplayer->activemenumode != AMMODE_EDIT) {
					if (slotnum == 4) {
						if (g_AmMenus[g_AmIndex].returntimer <= 0) {
							g_AmMenus[g_AmIndex].returntimer = 0;
							g_AmMenus[g_AmIndex].slotnum = slotnum;
						} else {
							g_AmMenus[g_AmIndex].returntimer--;
						}
					} else {
						bool gotoslot = true;
						char text[28];
						u32 flags;

						amGetSlotDetails(slotnum, &flags, text);

						if (strcmp(text, "") == 0) {
							gotoslot = false;
						}

						// If focusing a corner slot with C buttons or J pad,
						// set a special timer for the release. The player is
						// unlikely to release both C buttons on the same frame,
						// so this gives a bit of grace and prevents accidental
						// movement to a neighbouring slot.
						if (g_AmMenus[g_AmIndex].slotnum != 4
								&& !stickpushed
								&&
								(g_AmMenus[g_AmIndex].slotnum == 0
								 || g_AmMenus[g_AmIndex].slotnum == 2
								 || g_AmMenus[g_AmIndex].slotnum == 6
								 || g_AmMenus[g_AmIndex].slotnum == 8)) {
							if (slotnum != g_AmMenus[g_AmIndex].fromslotnum) {
								g_AmMenus[g_AmIndex].cornertimer = 2;
								g_AmMenus[g_AmIndex].fromslotnum = slotnum;
								gotoslot = false;
							}

							if (g_AmMenus[g_AmIndex].cornertimer > 0 && gotoslot) {
								gotoslot = false;
								g_AmMenus[g_AmIndex].cornertimer--;
							}
						}

						if (gotoslot) {
							g_AmMenus[g_AmIndex].returntimer = 15;
							g_AmMenus[g_AmIndex].slotnum = slotnum;
						}
					}
				}
			}
		}
#ifndef PLATFORM_N64
		else {
			g_AmMenus[g_AmIndex].mousex = 0.f;
			g_AmMenus[g_AmIndex].mousey = 0.f;
		}
#endif

		if (g_Vars.currentplayer->activemenumode != AMMODE_EDIT) {
			s16 dist;
			s16 dstradius;

			if (g_AmMenus[g_AmIndex].dstx != -123) {
				s16 dist;

				// Update selection x/y values
				g_AmMenus[g_AmIndex].selx = (g_AmMenus[g_AmIndex].selx + g_AmMenus[g_AmIndex].dstx) / 2;
				g_AmMenus[g_AmIndex].sely = (g_AmMenus[g_AmIndex].sely + g_AmMenus[g_AmIndex].dsty) / 2;

				dist = g_AmMenus[g_AmIndex].selx - g_AmMenus[g_AmIndex].dstx;

				if (dist <= 1 && dist >= -1) {
					g_AmMenus[g_AmIndex].selx = g_AmMenus[g_AmIndex].dstx;
				}

				dist = g_AmMenus[g_AmIndex].sely - g_AmMenus[g_AmIndex].dsty;

				if (dist <= 1 && dist >= -1) {
					g_AmMenus[g_AmIndex].sely = g_AmMenus[g_AmIndex].dsty;
				}
			}

			// Update x radius (the expanding effect when a new screen is loaded)
			dstradius = g_AmMenus[g_AmIndex].slotwidth + 5;

			g_AmMenus[g_AmIndex].xradius = (g_AmMenus[g_AmIndex].xradius * 3 + dstradius) / 4;

			dist = g_AmMenus[g_AmIndex].xradius - dstradius;

			if (dist <= 1 && dist >= -1) {
				g_AmMenus[g_AmIndex].xradius = dstradius;
			}

			// Update alpha of slots so they fade in
			if (g_AmMenus[g_AmIndex].alphafrac < 1) {
				g_AmMenus[g_AmIndex].alphafrac += (f32)g_Vars.lvupdate240 / (4.f * 30.0f);
			}

			if (g_AmMenus[g_AmIndex].alphafrac > 1) {
				g_AmMenus[g_AmIndex].alphafrac = 1;
			}

			// Make selection border pulsate
			g_AmMenus[g_AmIndex].selpulse += (f32)g_Vars.lvupdate240 / (4.f * 5.0f);

			if (g_AmMenus[g_AmIndex].selpulse > 18.849555969238f) {
				g_AmMenus[g_AmIndex].selpulse -= 18.849555969238f;
			}
		}
	}

	setCurrentPlayerNum(prevplayernum);
}
