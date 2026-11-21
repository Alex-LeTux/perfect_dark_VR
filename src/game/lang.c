#include <ultra64.h>
#include "constants.h"
#include "game/file.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/utils.h"
#include "bss.h"
#include "lib/dma.h"
#include "lib/main.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#ifndef PLATFORM_N64
#include "video.h"
#endif

/**
 * Officially, the NTSC versions are American English only, while the PAL
 * versions support British English, French, German, Italian and Spanish, and
 * the Japanese version is Japanese only. However all versions actually have
 * support for all 7 languages, despite some being inaccessible and not even
 * translated.
 *
 * All strings are categorised into areas, which we call banks. There is one
 * bank per stage, as well as some common banks such as menus and weapon names.
 * Each bank contains 7 files; one for each translation.
 *
 * An example language filename is LoptionsE. "options" is the bank name, and E
 * means it's the American English file.
 *
 * The list of suffixes are:
 * E - American English
 * J - Japanese
 * P - British English (P is for PAL)
 * _str_f = French
 * _str_g = German
 * _str_i = Italian
 * _str_s = Spanish
 *
 * Banks are loaded based on the current stage. When loading a bank, only the
 * current translation is loaded.
 *
 * Text is referenced using a 16 bit text ID. The text ID is made up of a 7-bit
 * bank index and a 9-bit string index within that bank:
 *
 *     bbbbbbbs ssssssss
 *
 * For versions prior to PAL, the language files are loaded during stage load
 * and allocated from memp. This method doesn't work for PAL because PAL
 * supports changing the language mid-stage and memp allocations cannot be freed
 * until a new stage is loaded. So for PAL, during stage load a single buffer is
 * allocated from memp to hold all loaded language files. The buffer is
 * repopulated when the language is changed, and the lang banks point to their
 * relevant offsets within that buffer.
 */

#if VERSION >= VERSION_PAL_BETA
bool g_Jpn = VERSION == VERSION_JPN_FINAL ? true : false;
u8 *g_LangBuffer = NULL;
u8 *g_LangBufferPos = NULL;
s32 g_LangBufferSize = 0;
uintptr_t *g_LangBanks[69];
struct jpncharpixels *g_JpnCharCachePixels;
struct jpncacheitem *g_JpnCacheCacheItems;
s32 g_LanguageId = LANGUAGE_NTSC_EN;
#else
uintptr_t *g_LangBanks[69];
struct jpncharpixels *g_JpnCharCachePixels;
struct jpncacheitem *g_JpnCacheCacheItems;
bool g_Jpn = false;
#endif

u16 g_LangFiles[] = {
	/* 0*/ 0,
	/* 1*/ FILE_LAMEE,
	/* 2*/ FILE_LARCHE,
	/* 3*/ FILE_LARKE,
	/* 4*/ FILE_LASHE,
	/* 5*/ FILE_LAZTE,
	/* 6*/ FILE_LCATE,
	/* 7*/ FILE_LCAVEE,
	/* 8*/ FILE_LARECE,
	/* 9*/ FILE_LCRADE,
	/*10*/ FILE_LCRYPE,
	/*11*/ FILE_LDAME,
	/*12*/ FILE_LDEPOE,
	/*13*/ FILE_LDESTE,
	/*14*/ FILE_LDISHE,
	/*15*/ FILE_LEARE,
	/*16*/ FILE_LELDE,
	/*17*/ FILE_LIMPE,
	/*18*/ FILE_LJUNE,
	/*19*/ FILE_LLEEE,
	/*20*/ FILE_LLENE,
	/*21*/ FILE_LLIPE,
	/*22*/ FILE_LLUEE,
	/*23*/ FILE_LOATE,
	/*24*/ FILE_LPAME,
	/*25*/ FILE_LPETEE,
	/*26*/ FILE_LREFE,
	/*27*/ FILE_LRITE,
	/*28*/ FILE_LRUNE,
	/*29*/ FILE_LSEVBE,
	/*30*/ FILE_LSEVE,
	/*31*/ FILE_LSEVXE,
	/*32*/ FILE_LSEVXBE,
	/*33*/ FILE_LSHOE,
	/*34*/ FILE_LSILOE,
	/*35*/ FILE_LSTATE,
	/*36*/ FILE_LTRAE,
	/*37*/ FILE_LWAXE,
	/*38*/ FILE_LGUNE,
	/*39*/ FILE_LTITLEE,
	/*40*/ FILE_LMPMENUE,
	/*41*/ FILE_LPROPOBJE,
	/*42*/ FILE_LMPWEAPONSE,
	/*43*/ FILE_LOPTIONSE,
	/*44*/ FILE_LMISCE,
	/*45*/ FILE_LUFFE,
	/*46*/ FILE_LOLDE,
	/*47*/ FILE_LATEE,
	/*48*/ FILE_LLAME,
	/*49*/ FILE_LMP1E,
	/*50*/ FILE_LMP2E,
	/*51*/ FILE_LMP3E,
	/*52*/ FILE_LMP4E,
	/*53*/ FILE_LMP5E,
	/*54*/ FILE_LMP6E,
	/*55*/ FILE_LMP7E,
	/*56*/ FILE_LMP8E,
	/*57*/ FILE_LMP9E,
	/*58*/ FILE_LMP10E,
	/*59*/ FILE_LMP11E,
	/*60*/ FILE_LMP12E,
	/*61*/ FILE_LMP13E,
	/*62*/ FILE_LMP14E,
	/*63*/ FILE_LMP15E,
	/*64*/ FILE_LMP16E,
	/*65*/ FILE_LMP17E,
	/*66*/ FILE_LMP18E,
	/*67*/ FILE_LMP19E,
	/*68*/ FILE_LMP20E,
};

u32 langGetLangBankIndexFromStagenum(s32 stagenum)
{
	u32 bank;

	switch (stagenum) {
	case STAGE_PELAGIC:       bank = LANGBANK_DAM; break;
	case STAGE_EXTRACTION:    bank = LANGBANK_ARK; break;
	case STAGE_TEST_RUN:      bank = LANGBANK_RUN; break;
	case STAGE_24:            bank = LANGBANK_SEVX; break;
	case STAGE_MAIANSOS:      bank = LANGBANK_SEV; break;
	case STAGE_TEST_DEST:     bank = LANGBANK_DEST; break;
	case STAGE_2B:            bank = LANGBANK_SEVXB; break;
	case STAGE_RETAKING:      bank = LANGBANK_SEVB; break;
	case STAGE_WAR:           bank = LANGBANK_STAT; break;
	case STAGE_CHICAGO:       bank = LANGBANK_PETE; break;
	case STAGE_G5BUILDING:    bank = LANGBANK_DEPO; break;
	case STAGE_ESCAPE:        bank = LANGBANK_TRA; break;
	case STAGE_MP_TEMPLE:     bank = LANGBANK_JUN; break;
	case STAGE_MP_PIPES:      bank = LANGBANK_CRAD; break;
	case STAGE_MP_G5BUILDING: bank = LANGBANK_CRYP; break;
	case STAGE_CITRAINING:    bank = LANGBANK_DISH; break;
	case STAGE_MP_COMPLEX:    bank = LANGBANK_REF; break;
	case STAGE_MP_SKEDAR:     bank = LANGBANK_OAT; break;
	case STAGE_TEST_LEN:      bank = LANGBANK_LEN; break;
	case STAGE_DEFECTION:     bank = LANGBANK_AME; break;
	case STAGE_VILLA:         bank = LANGBANK_ELD; break;
	case STAGE_DEFENSE:       bank = LANGBANK_IMP; break;
	case STAGE_TEST_ASH:      bank = LANGBANK_ASH; break;
	case STAGE_INVESTIGATION: bank = LANGBANK_EAR; break;
	case STAGE_ATTACKSHIP:    bank = LANGBANK_LEE; break;
	case STAGE_RESCUE:        bank = LANGBANK_LIP; break;
	case STAGE_INFILTRATION:  bank = LANGBANK_LUE; break;
	case STAGE_28:            bank = LANGBANK_DAM; break;
	case STAGE_DEEPSEA:       bank = LANGBANK_PAM; break;
	case STAGE_SKEDARRUINS:   bank = LANGBANK_SHO; break;
	case STAGE_AIRFORCEONE:   bank = LANGBANK_RIT; break;
	case STAGE_TEST_ARCH:     bank = LANGBANK_ARCH; break;
	case STAGE_MP_RAVINE:     bank = LANGBANK_AREC; break;
	case STAGE_CRASHSITE:     bank = LANGBANK_AZT; break;
	case STAGE_AIRBASE:       bank = LANGBANK_CAVE; break;
	case STAGE_TEST_UFF:      bank = LANGBANK_UFF; break;
	case STAGE_TEST_OLD:      bank = LANGBANK_OLD; break;
	case STAGE_DUEL:          bank = LANGBANK_ATE; break;
	case STAGE_TEST_LAM:      bank = LANGBANK_LAM; break;
	case STAGE_MP_BASE:       bank = LANGBANK_MP1; break;
	case STAGE_TEST_MP2:      bank = LANGBANK_MP2; break;
	case STAGE_MP_AREA52:     bank = LANGBANK_MP3; break;
	case STAGE_MP_WAREHOUSE:  bank = LANGBANK_MP4; break;
	case STAGE_MP_CARPARK:    bank = LANGBANK_MP5; break;
	case STAGE_TEST_MP6:      bank = LANGBANK_MP6; break;
	case STAGE_TEST_MP7:      bank = LANGBANK_MP7; break;
	case STAGE_TEST_MP8:      bank = LANGBANK_MP8; break;
	case STAGE_MP_RUINS:      bank = LANGBANK_MP9; break;
	case STAGE_MP_SEWERS:     bank = LANGBANK_MP10; break;
	case STAGE_MP_FELICITY:   bank = LANGBANK_MP11; break;
	case STAGE_MP_FORTRESS:   bank = LANGBANK_MP12; break;
	case STAGE_MP_VILLA:      bank = LANGBANK_MP13; break;
	case STAGE_TEST_MP14:     bank = LANGBANK_MP14; break;
	case STAGE_MP_GRID:       bank = LANGBANK_MP15; break;
	case STAGE_TEST_MP16:     bank = LANGBANK_MP16; break;
	case STAGE_TEST_MP17:     bank = LANGBANK_MP17; break;
	case STAGE_TEST_MP18:     bank = LANGBANK_MP18; break;
	case STAGE_TEST_MP19:     bank = LANGBANK_MP19; break;
	case STAGE_TEST_MP20:     bank = LANGBANK_MP20; break;
	case STAGE_MBR:           bank = LANGBANK_WAX; break;
	case STAGE_TEST_SILO:     bank = LANGBANK_SILO; break;
	default:
		while (true) {
			// empty
		}
	}

	return bank;
}

extern u8 EXT_SEG _fontjpnSegmentRomStart;
extern u8 EXT_SEG _fontjpnsingleSegmentRomStart;
extern u8 EXT_SEG _fontjpnmultiSegmentRomStart;

struct jpncharpixels *langGetJpnCharPixels(s32 codepoint)
{
	s32 i;
	s32 freeindexsingle = -1;
	s32 freeindexmulti = -1;
	bool multibyte = false;

#if VERSION == VERSION_JPN_FINAL
	static u32 tload = 0;
	static u32 tmul = 8;
#endif

	if (codepoint & 0x2000) {
		multibyte = true;
	}

#if VERSION == VERSION_JPN_FINAL
	mainOverrideVariable("tmul", &tmul);
	mainOverrideVariable("tload", &tload);

	if (tload) {
		codepoint = tload;
	}
#endif

#if VERSION == VERSION_JPN_FINAL
#define SHIFTAMOUNT 0
#define TMUL tmul
#else
#define SHIFTAMOUNT 1
#define TMUL 8
#endif

	for (i = 0; i < MAX_JPN_CACHE_ITEMS(); i++) {
		if (!multibyte && (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i].codepoint) {
			break;
		}

		if (multibyte && i + 1 < MAX_JPN_CACHE_ITEMS()
				&& (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i].codepoint
				&& (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i + 1].codepoint) {
			break;
		}

		if (g_JpnCacheCacheItems[i].ttl == 0) {
			freeindexsingle = i;
		}

		if (g_JpnCacheCacheItems[i].ttl == 0 && g_JpnCacheCacheItems[i + 1].ttl == 0 && i + 1 < MAX_JPN_CACHE_ITEMS()) {
			freeindexmulti = i;
		}
	}

	if (i < MAX_JPN_CACHE_ITEMS()) {
		if (!multibyte) {
			g_JpnCacheCacheItems[i].ttl = 2;

			return &g_JpnCharCachePixels[i * TMUL];
		} else {
			g_JpnCacheCacheItems[i + 0].ttl = 2;
			g_JpnCacheCacheItems[i + 1].ttl = 2;

			return &g_JpnCharCachePixels[TMUL * i];
		}
	}

#if VERSION == VERSION_JPN_FINAL
	if (!multibyte && freeindexsingle >= 0) {
		g_JpnCacheCacheItems[freeindexsingle].ttl = 2;
		g_JpnCacheCacheItems[freeindexsingle].codepoint = codepoint;

		dmaExec((u8 *) (freeindexsingle * (TMUL * 0x0c) + (romptr_t) &g_JpnCharCachePixels[0]),
				(romptr_t) REF_SEG _fontjpnSegmentRomStart + ((codepoint * TMUL) * 0xc + TMUL * (24 * 0xc)),
				TMUL * 0x0c);

#ifndef PLATFORM_N64
		// indicate that this is a new texture
		videoFreeCachedTexture(&g_JpnCharCachePixels[TMUL * freeindexsingle]);
#endif

		return &g_JpnCharCachePixels[TMUL * freeindexsingle];
	}

	if (multibyte && freeindexmulti >= 0) {
		g_JpnCacheCacheItems[freeindexmulti + 0].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 1].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 0].codepoint = codepoint;
		g_JpnCacheCacheItems[freeindexmulti + 1].codepoint = codepoint;
		return &g_JpnCharCachePixels[0];
	} else {
		var8009d370jf++;
		return &g_JpnCharCachePixels[0];
	}
#else
	if (!multibyte && freeindexsingle >= 0) {
		g_JpnCacheCacheItems[freeindexsingle].ttl = 2;
		g_JpnCacheCacheItems[freeindexsingle].codepoint = codepoint >> 1;

		dmaExec(&g_JpnCharCachePixels[freeindexsingle * 8], (romptr_t) REF_SEG _fontjpnsingleSegmentRomStart + (codepoint >> SHIFTAMOUNT) * 0x60, 0x60);

#ifndef PLATFORM_N64
		// indicate that this is a new texture
		videoFreeCachedTexture(&g_JpnCharCachePixels[freeindexsingle * 8]);
#endif

		return &g_JpnCharCachePixels[freeindexsingle * 8];
	}

	if (multibyte && freeindexmulti >= 0) {
		g_JpnCacheCacheItems[freeindexmulti + 0].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 1].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 0].codepoint = codepoint >> 1;
		g_JpnCacheCacheItems[freeindexmulti + 1].codepoint = codepoint >> 1;

		dmaExec(&g_JpnCharCachePixels[freeindexmulti * 8], (romptr_t) REF_SEG _fontjpnmultiSegmentRomStart + ((codepoint & 0x1fff) >> SHIFTAMOUNT) * 0x80, 0x80);

#ifndef PLATFORM_N64
		// indicate that this is a new texture
		videoFreeCachedTexture(&g_JpnCharCachePixels[freeindexmulti * 8]);
#endif

		return &g_JpnCharCachePixels[freeindexmulti * 8];
	}

	return &g_JpnCharCachePixels[0];
#endif
}

/**
 * NTSC only supports English, while PAL supports 4 languages and JPN has its
 * own. Each English file is followed immediately by the other translations.
 */
s32 langGetFileNumOffset(void)
{
#if PAL
	s32 offset = g_LanguageId;
	return offset + 2;
#else
	s32 offset = 0;

	if (g_Jpn) {
		offset = 1;
	}

	return offset;
#endif
}

s32 langGetFileId(s32 bank)
{
	return g_LangFiles[bank] + langGetFileNumOffset();
}

void langLoad(s32 bank)
{
#if VERSION >= VERSION_PAL_BETA
	s32 len = fileGetInflatedSize(langGetFileId(bank), LOADTYPE_LANG);

	if ((uintptr_t)g_LangBuffer + len + g_LangBufferSize - (uintptr_t)g_LangBufferPos >= 0) {
		s32 len2 = (uintptr_t)g_LangBuffer + g_LangBufferSize - (uintptr_t)g_LangBufferPos;
		len2 = len2 / 32 * 32;
		g_LoadType = LOADTYPE_LANG;
		g_LangBanks[bank] = fileLoadToAddr(langGetFileId(bank), FILELOADMETHOD_DEFAULT, (u8 *)g_LangBufferPos, len2);
		g_LangBufferPos = (u8 *)(align32((uintptr_t)g_LangBufferPos + len));
	} else {
		CRASH();
	}
#else
	s32 file_id = langGetFileId(bank);
	g_LoadType = LOADTYPE_LANG;
	g_LangBanks[bank] = fileLoadToNew(file_id, FILELOADMETHOD_DEFAULT, LOADTYPE_LANG);
#endif
}

void langLoadToAddr(s32 bank, u8 *dst, s32 size)
{
	s32 file_id = langGetFileId(bank);
	g_LoadType = LOADTYPE_LANG;
	g_LangBanks[bank] = fileLoadToAddr(file_id, FILELOADMETHOD_DEFAULT, dst, size);
}

void langClearBank(s32 bank)
{
	g_LangBanks[bank] = NULL;
}

extern bool VrManualReloading;

/**
 * VR holsters (Task C6f): the ROM texts that name the controls manual reload and the holsters
 * changed, with the controls they have now: use is the A or X tap (vrHolsterFaceButtons), the
 * grab the grip, a held gadget comes from the chest pouch and a device toggle from the pause
 * menu. langGet returns these only with VrManualReloading on, and only in the NTSC final English
 * texts (not with the Japanese files, g_Jpn): the other versions and languages keep the ROM's.
 * Task H1: the texts that name the N64 Z, R and C buttons (the firing range hints, the holotraining
 * descriptions and hints, the ECM Mine's description and device training tips): fire is the trigger, a fist the grip, aim is pointing the
 * gun (a grip on a gun only holds it), looking is the head, ducking is the body (or the left stick
 * click, Seated Mode or "Left Stick Click to Crouch"), sidestepping the left stick.
 */
#if VERSION == VERSION_NTSC_FINAL
struct vrlangoverride {
	s32 textid;
	char *text;
};

static struct vrlangoverride g_VrLangOverrides[] = {
	{ L_DISH_056, "Select the Uplink.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n" },
	{ L_DISH_057, "Select the CamSpy.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_058, "Select the Night Vision.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_059, "Select the Door Decoder.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n" },
	{ L_DISH_060, "Select the X-Ray Scanner.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_061, "Select the Disguise.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to put it on.\n" },
	{ L_DISH_062, "Select the IR Scanner.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_063, "Select the Tracker.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_064, "Select the Cloaking Device.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n- Pull the trigger to switch it on or off.\n" },
	{ L_DISH_065, "Select the ECM Mine.\n- Tap A or X with a hand at your chest to cycle through your gadgets.\n- Grip your chest to equip the gadget.\n" },
	{ L_DISH_073, "HOVERCRATE\nGrip with an empty hand next to Hovcrate to grab it...\n" },
	{ L_DISH_074, "Let go of the grip when crate grabbed to release it.\n" },
	{ L_DISH_075, "HOVERBIKE\nDouble tap A or X next to Hovbike to ride it...\n" },
	{ L_DISH_076, "Double tap A or X when on Hovbike to dismount.\n" },
	{ L_DISH_077, "OPENING DOORS\nPress A or X next to door to open it.\n" },
	{ L_DISH_078, "A or X will also activate any object nearby.\n" },
	{ L_DISH_079, "ELEVATORS\nPress A or X next to door to call elevator.\n" },
	{ L_DISH_095, "DUCK\n- Lower your head to duck.\n- Or click the left stick (with Left Stick Click to Crouch, or Seated Mode).\n" },
	{ L_DISH_096, "CROUCH\n- When ducking, squat lower to crouch.\n- Or click the left stick again (with Left Stick Click to Crouch, or Seated Mode).\n" },
	{ L_DISH_107, "Unarmed combat - knock out the target.\n- Hold the grip to make a fist, then punch.\n" },
	{ L_DISH_114, "TERMINALS\n- Press A or X next to terminal to use it.\n" },
	{ L_MISC_271, "Description\n- A tiny remote camera for stealthy exploration. Equipped for spectroscopic holography. Opens doors by projecting a human-sized pulse of heat.\n\nTraining Instructions\n- Holograph the hacker's terminal next door in the Info room.\n\nOperation\n- Pull the left trigger to take a holograph. Press A to open any doors in the way, and B to return to Joanna. Move with the left stick, rise and fall with the right stick, and look around with your head.\n" },
	{ L_MISC_273, "Description\n\n- Stand-alone code-breaking device. Attaches to the control panel and sifts through the possible combinations until the lock is opened.\n\nTraining Instructions\n- Find the pad by the locked door and use the Door Decoder on it to unlock the door.\n\nOperation\n- Stand next to the door pad and press A or X (or pull the trigger) while holding the Door Decoder to use it.\n" },
	{ L_MISC_279, "Description\n- Emits a constantly shifting signal designed to jam any electronic communications device. Must be placed on the object to be effective.\n\nTraining Instructions\n- Throw the ECM Mine onto the lighting hub located through the secret door.\n\nOperation\n- Hold the trigger, swing your arm toward the target and let go of the trigger to throw the mine.\n" },
	{ L_MISC_280, "Description\n- Provides a link from the field agent to the Institute hackers back at HQ, who can then download data or crack electronic locks remotely.\n\nTraining Instructions\n- Use the Data Uplink to hack the terminal in the corner, unlocking a secret door.\n\nOperation\n- Stand next to the terminal and press A or X (or pull the trigger) when holding Data Uplink.\n" },
	{ L_MISC_281, "FIRING\n Pull the trigger to fire gun.\n" },
	{ L_MISC_282, "RELOAD\n When out of ammo, press B or Y and reload by hand.\n" },
	{ L_MISC_283, "MANUAL RELOAD\n Press B or Y to drop the clip early if not empty.\n" },
	{ L_MISC_284, "Aiming: Point the gun where you want to shoot.\n" },
	{ L_MISC_285, "The aiming sight follows the gun as your hand moves.\n" },
	{ L_MISC_286, "AUTO FIRE\n Hold the trigger to repeatedly fire automatically.\n" },
	{ L_MISC_287, "ALTER AIM\n Tilt the gun up or down to move sight up/down.\n" },
	{ L_MISC_288, "ZOOM\n Look through the gun's scope to zoom in.\n" },
	{ L_MISC_289, "FAST FIRE\n Pull the trigger quickly to fire faster.\n" },
	{ L_MISC_336, "Description\n- A good agent notices everything around him or her.\n\nTraining Instructions\n- Examine all of the objects by looking directly at them.\n\nOperation\n-Look up- Tilt your head up.\n-Look down- Tilt your head down.\n-Free Look- Turn your head to look around.\n" },
	{ L_MISC_337, "Description\n- Sidestepping and strafing can get you out of trouble.\n\nTraining Instructions\n- Activate all of the switches in front. Be quick, though, as each switch will reset on a time limit.\n\nOperation\n- Sidestep left- Push the left stick left\n- Sidestep right- Push the left stick right\n\n" },
	{ L_MISC_338, "Description\n- Ducking and crouching can open up new areas for exploration.\n\nTraining Instructions\n- Work your way through the obstacles using the moves available to you. Activate the switches.\n\nOperation\n- Duck- Lower your head.\n- Crouch- When ducking, squat lower.\n- Get up- Stand up straight.\n- With Left Stick Click to Crouch, or Seated Mode, each click of the left stick ducks, crouches, then stands.\n\n" },
	{ L_MISC_339, "Description\n- Fighting multiple opponents in hand-to-hand combat can be difficult.\n\nTraining Instructions\n- Knock out all enemies without getting hit.\n\nOperation\n- Punch - With an empty hand, hold the grip to make a fist and swing it again and again to launch a flurry of punches.\n\n" },
	{ L_MISC_340, "Description\n- Disarming an enemy can get you a new weapon.\n\nTraining Instructions\n- Knock out/disarm all enemies without getting hit.\n\nOperation\n- Disarm - Hold the B Button and pull the trigger, then punch an enemy to disarm it.\n- Knockout Punch - Punch an unaware enemy from behind.\n" },
	{ L_MISC_358, "Be sure the mine will land in the correct place by getting as close as you can to the target. Aim the throw by swinging your arm toward the target.\n" },
	{ L_MISC_368, "Be sure the mine will land in the correct place by getting as close as you can to the target. Aim the throw by swinging your arm toward the target.\n" },
};
#endif

/**
 * Resolve a text ID to a string.
 *
 * g_LangBanks is an array of pointers to language file data in RAM. Many of
 * those pointers will be NULL because only the necessary language files are
 * loaded at any given time.
 *
 * The language file data consists of a variable-length array of offsets into
 * the file. Not to be confused with pointers.
 */
char *langGet(s32 textid)
{
	s32 bankindex = textid >> 9;
	s32 textindex = textid & 0x1ff;
	uintptr_t *bank = (uintptr_t*)g_LangBanks[bankindex];
	uintptr_t addr;

#if VERSION == VERSION_NTSC_FINAL
	if (VrManualReloading && !g_Jpn) {
		s32 i;

		for (i = 0; i < ARRAYCOUNT(g_VrLangOverrides); i++) {
			if (g_VrLangOverrides[i].textid == textid) {
				return g_VrLangOverrides[i].text;
			}
		}
	}
#endif

	if (bank && bank[textindex]) {
		addr = (uintptr_t)bank + bank[textindex];
	} else {
		addr = 0;
	}

	return (char *)addr;
}

#if VERSION >= VERSION_PAL_BETA
void langReload(void)
{
	s32 i;

	g_LangBufferPos = (u8 *) align32((uintptr_t) g_LangBuffer);

	for (i = 0; i < ARRAYCOUNT(g_LangBanks); i++) {
		if (g_LangBanks[i] != NULL) {
			langLoad(i);
		}
	}
}
#endif

#if VERSION >= VERSION_PAL_BETA
void langSetEuropean(u32 arg0)
{
	u8 teams;
	bool hasoptionslang = false;

	if (g_LangBanks[LANGBANK_OPTIONS] != NULL) {
		hasoptionslang = true;
	}

	if (hasoptionslang) {
		mpGetTeamsWithDefaultName(&teams);
	}

	switch (arg0) {
	case 0:
		g_LanguageId = LANGUAGE_PAL_EN;
		break;
	case 1:
		g_LanguageId = LANGUAGE_PAL_DE;
		break;
	case 2:
		g_LanguageId = LANGUAGE_PAL_FR;
		break;
	case 3:
		g_LanguageId = LANGUAGE_PAL_ES;
		break;
	case 4:
		g_LanguageId = LANGUAGE_PAL_IT;
		break;
	case 5:
		g_LanguageId = -2;
		break;
	}

	langReload();

	if (hasoptionslang) {
		mpSetTeamNamesToDefault(teams);
	}
}
#endif

#if VERSION == VERSION_JPN_FINAL
void langSetJpnEnabled(bool enable)
{
	g_Jpn = enable ? true : false;

	langReload();
}
#endif
