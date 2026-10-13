// Apply Time Traveler (tt061891.bin) labels, functions and comments.
// Source of every address: docs/ROM_FINDINGS.md (pass 1, Capstone).
// Addresses use 80188 real-mode seg:off notation.
// @category TimeTraveler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class TimeTravelerLabels extends GhidraScript {

    // { seg:off, name, plate comment }
    private static final String[][] FUNCS = {
        {"F000:FFF0", "reset_vector", "UMCS=0xC03C (256KB ROM), ljmp C000:0000"},
        {"C000:0000", "crt_start", "80188 PCB setup, LED test, RAM test, DGROUP copy DBE1:0000 -> 01EB:0000 len 0x1C90"},
        // MSC runtime, segment DB9B
        {"DB9B:0000", "strcat", "far strcat(dst, src)"},
        {"DB9B:0046", "strcpy", "far strcpy(dst, src)"},
        {"DB9B:0124", "itoa", "itoa(int val, char far *buf, int radix)"},
        {"DB9B:0140", "ltoa", "ltoa(long val, char far *buf, int radix)"},
        {"DB9B:014A", "inp", "inp(port) -- all port reads go through here"},
        {"DB9B:0158", "outp", "outp(port, val) -- all port writes go through here"},
        // Serial transport, segment D30C
        {"D30C:0002", "uart_init", "LCR=0x83, DLL=10, DLM=0, LCR=3 -> 768kHz/16/10 = 4800 8N1"},
        {"D30C:0071", "ld_send", "send NUL-terminated command; Sony path if g_ld_is_sony"},
        {"D30C:0106", "ld_send_b", "ld_send variant used by ld_audio"},
        {"D30C:019B", "ld_send_raw", ""},
        {"D30C:020B", "ld_send_echo", "waits LSR&0x60 (THRE|TEMT) per byte"},
        {"D30C:02AB", "ld_send_sony", ""},
        {"D30C:0331", "ld_cmd_retry", "loops ld_cmd_once until nonzero; arg3 = timeout"},
        {"D30C:035C", "ld_cmd_once", "send, then read reply"},
        {"D30C:03C4", "ld_read_reply", ""},
        {"D30C:0490", "ld_wait_reply", "timeout wait"},
        {"D30C:05C7", "ld_rx_flush", "UNVERIFIED NAME: called before each send"},
        // pioneer.c, segment D37E
        {"D37E:03AB", "ld_search_play_to", "(start, stop). Pioneer: '<start>SE\\r' then '<stop>SMPL1VD\\r'. start==-1 skips search"},
        {"D37E:04FA", "ld_play_to", "(start, stop). Pioneer: '<stop>SM' ... 'PL'"},
        {"D37E:0615", "ld_multispeed_fwd", "uses '184SPMF'"},
        {"D37E:0708", "ld_still", "'ST'"},
        {"D37E:08F9", "ld_search", "(frame). Pioneer '<frame>SE1VD'"},
        {"D37E:0ACA", "ld_play_speed_fwd", "'<n>SP' + 'SMMF'"},
        {"D37E:0F2B", "ld_audio", "(mask) bit0=ch1 bit1=ch2. Pioneer '<mask>AD'"},
        // specobj.c
        {"D47D:1D6C", "specobj_play", "looks up key in specobj_table {key,audio,start,stop}"},
        // asset.c, segment C4B6 (module by link order, see ROM_FINDINGS)
        {"C4B6:0035", "asset_find", "(asset_id) -> far ptr to first 12-byte asset record with that id, or 0"},
        {"C4B6:0081", "maybe_asset_table_ok", "counts asset records; returns 1 if count < 0x1C2"},
        {"C4B6:00CB", "clip_find", "(clip_id) -> far ptr to 8-byte clip record {id,start,stop,u8 kind,u8 audio}, or 0"},
        {"C4B6:0158", "asset_dispatch", "(type, ref, offset): 1 = clip_play(clip_find(ref)); 3 = D47D:00F3(ref); 4 = DA9D:0002()"},
        {"C4B6:020B", "clip_play", "(clip*, offset): ld_audio(clip.audio) then ld_search_play_to(start + f(offset), stop)"},
        {"C4B6:03FC", "asset_play_at", "(t): picks asset record with lo <= t < hi, marks it seen, asset_dispatch(type, ref, t - lo)"},
        // input.c, segment CE55
        {"CE55:02F5", "rule_find", "(key) -> first 8-byte input rule {key, move|0x8000, window, action} at C52E:2FB6"},
        {"CE55:0341", "maybe_rule_table_ok", "counts rules; 1 if < 0x5DC"},
        {"CE55:03CC", "move_match", "(device, bits, move*): mode 1 single, 2 any row, 3 all rows in sequence (timer)"},
        {"CE55:0575", "rule_action", "(rule*, phase) -> action_do(rule.action, phase)"},
        {"CE55:059F", "move_find", "(move_id) -> first 8-byte move row {id, mode, device, bits} at C52E:79AA"},
        // window.c, segment CFD9
        {"CFD9:0002", "window_find", "(id) -> 10-byte window {id, open_a, close_a, open_b, close_b} at C52E:07C4"},
        {"CFD9:0082", "input_check_rules", "walks rules sharing a key: in window? move_match? -> rule_action; marks window done per player"},
        {"CFD9:0249", "window_is_open", "(rule*, t): open = +6, close = +8 (or +4 by per-player flag 0x13C4)"},
        // action.c, segment D047
        {"D047:000C", "action_find", "(code) -> first 12-byte action {code, u8 phase, u8 argc, far fn, arg1, arg2} at C52E:2D3A"},
        {"D047:0058", "action_do", "(code, phase): code & 0xF000 == 0 -> table actions with that phase; else built-in D047:00E6"},
        {"D047:00E6", "action_builtin", "code = N<<12|hi6<<6|lo6; switch N via cs:026A (N=2..8): ok/score/death n. See ROM_FINDINGS"},
        {"D047:04F2", "episode_fail_to_death", "(n): mark episode failed (2), enter death sub-record n of current episode, message 0x8D"},
        {"D047:0761", "score_add", "score += cur_episode.points (x2 if score <= 90000, x1 to 100000, x10 above)"},
        {"DA9D:0002", "beep_ok", "tone 0x9C4, length 4; returns 2"},
        {"C40B:0753", "death_find", "(n): from current header, next kind-10003 record with ep == n"},
        {"C40B:08A8", "episode_enter", "(rec, t): make rec current, play its asset at t, arm its input rules"},
        {"C40B:0A45", "episode_rules", "(rec) -> rule_find(rec.f4)"},
        {"C40B:0AB2", "episode_asset", "(rec) -> asset_find(rec.f5)"},
        {"CFD9:0316", "window_expire_check", "(rules*, t): window closing uncleared -> rule_action(phase 2) for NO-MOVE rules and action nibble 6"},
        {"D17E:0002", "input_poll_buttons", "IN1 bits 4,5 -> g[0xC34] 0x100 Attack, 0x200 Cube; IN0 bits 4,5 -> 0x10/0x20 starts"},
        {"D17E:0121", "input_poll_joy_coin", "IN0 bits 0-3 -> g[0xC34] joystick U/D/L/R; bits 6,7 coin/bill -> credits"},
        {"D17E:02D5", "input_bits", "(device): 1 -> joystick nibble, 4 -> coin bits, 0x10 -> button bits"},
        {"D047:0284", "action_call", "calls the record's far fn"},
        // attract/config, segment C2BA
        {"C2BA:0470", "dips_read", "DSW1/2/3 -> g[0x2E..0x31]; derives lives, cubes, devil, difficulty, LD protocol"},
        {"C2BA:079D", "difficulty_set", "(0..3) -> g[0x1C], g[0x2C]"},
        {"C2BA:089E", "dip_field", "(mask_lo, mask_hi) -> (dips & mask) >> lowest set bit"},
        // chapter.c? segment D7C0
        {"D7C0:0008", "episode_done", "mark header cleared, ch1 ep20/21/30 linking, then chapter progress"},
        {"D047:03E5", "action_episode_end", "(1) -> episode_done; (4) reset; (7/8) counter g[0x434] < 7 ..."},
        {"D047:0679", "cube_rewind", "re-enter previous episode at max(0, g[0x16] - D245:07D8(3))"},
        // odds/chorder, segment C359
        {"C359:0034", "game_choose", "per player: bonus = rand()%4+2, clear flags, choose_episodes, choose_rare(bonus)"},
        {"C359:0000", "chance", "(p) -> rand() % 100 < p"},
        {"C359:00B6", "choose_episodes", "sets chosen flag per header by kind/weight; one weighted pick per 1000/1001 run"},
        {"C359:04B1", "episode_flags_clear", "clears per-player chosen (+0xA0) and status (+0x102C) arrays"},
        {"C359:056E", "is_bonus_chapter", "(rec) -> rec.chapter == [p*2+0x1578]"},
        {"C359:0620", "choose_rare", "(bonus): 50% none; else 3%/group/pass over DGROUP 0xCA substitution groups"},
        {"D70D:0917", "maybe_episode_find_ch_ep", "(chapter, episode) -> header record"},
        {"D668:05BB", "episode_c6e50_allowed", "special case for chapter 6 episode 50"},
        // lives / cubes / trader / hellgate
        {"DA9D:04AE", "lose_life", "lives-- unless chapter 0 or level select"},
        {"DA9D:064D", "maybe_hiscore_entry", "from game_over: score vs top-3 table (D945:01DD); qualifies -> plays LD 47768-48040 (DA9D:074E)"},
        {"D7C0:06A5", "after_death", "lose_life if failed episode had points; game-over check"},
        {"CE55:00C9", "input_event", "(device, bits): Cube during death -> cubes--, rewind; else rule matching"},
        {"CEDF:0719", "trader", "cube purchase screen, LD 28053-28188"},
        {"CEDF:0D80", "trader_coins", "credits -> cube purchases"},
        {"CEDF:0DF5", "trader_buy", "(n): g[0x88] credits per g[0x1928] cubes"},
        {"CEDF:0E8F", "cubes_add", "(n): cubes += n"},
        {"DA9D:0F3A", "cubes_grant_start", "(0|1): cubes = 1"},
        {"D0F0:025E", "hellgate", "devil slot machine: jackpot/life/cubes/lose/lose-all by rand()%100"},
        // chapter order / time map
        {"C3CE:0055", "chapter_order_init", "fixed slots 10000,0,1,..,7,8,20; slots 3-7 random from D372:0060 matrix"},
        {"C3CE:0368", "chapter_order_clear", "11 slots = -1"},
        {"C3CE:02AE", "maybe_chapter_unplaced", "(ch) -> 1 if ch not yet in this player's order"},
        {"D70D:0021", "chapter_advance", "next slot; eras via time_map"},
        {"D70D:0973", "maybe_chapter_completed", "(ch)"},
        {"D843:00B9", "time_map", "Time Map: completed mask, trader offer, LD cursor loop -> chosen chapter"},
        // branch / init: game over
        {"D7C0:0616", "maybe_mercy_open", "0 when an extra life may be granted at lives 0 (early game)"},
        {"D7C0:0791", "game_over_or_continue", "continue offer, else game_over + back_to_attract (1P) or player hand-over (2P)"},
        {"D7C0:052D", "game_over", "message 0x3D/0x3E, high score update, clear score"},
        {"D668:0406", "back_to_attract", "reset per-player state, attract chapter order"},
        {"D668:036F", "set_player", "(p): g[0xC3C] = p, resume chapter"},
        // two players
        {"D7C0:038F", "switch_player", "after a life lost in 2P: mercy, game over or msg 0x0E, toggle g[0xC3C]"},
        {"D7C0:068B", "set_player_count", "(n): g[0x72] = n"},
        // difficulty
        {"DA9D:0DE5", "good_episode_count", "[p*2+0x42E]++ ; == g[0x2C] -> windows_hard"},
        {"DA9D:00D0", "windows_hard", "[p*2+0x13C4] = 1: window variant b (+6..+8)"},
        {"DA9D:00F5", "windows_easy", "[p*2+0x13C4] = 0: window variant a (+2..+4)"},
        // episode.c, segment C40B
        {"C40B:004F", "maybe_episode_next", "next unplayed header in current chapter for player g[0xC3C]; odds via D668:05BB"},
        {"C40B:0482", "episode_find", "(ep) -> first episode record with +6 == ep, or 0"},
        {"C40B:04D0", "episode_test_select", "test mode: shows 'C<ch> E<ep>' on LED, joystick steps through headers"},
        {"C40B:0004", "maybe_episode_table_ok", "counts 14-byte episode records until +6 == -1; 1 if < 0x1C2"},
    };

    private static final String[][] DATA = {
        {"CC1A:0002", "clip_table", "348 x {u16 id, u16 start_frame, u16 stop_frame, u8 kind, u8 audio_mask}; id 0 = dummy; key 0 ends at CCC82"},
        {"CB0D:0006", "asset_table", "358 x {u16 asset_id, u16 type, u16 ref, u16 lo, u16 hi, u16 flags}; key 0 ends"},
        {"C5AA:0004", "window_table", "958 x {u16 id, open_a, close_a, open_b, close_b}"},
        {"C801:000A", "action_table", "52 x {u16 code, u8 phase, u8 argc, u16 fn_off, u16 fn_seg, arg1, arg2}"},
        {"C829:0006", "input_rule_table", "1478 x {u16 key, u16 move|0x8000, u16 window, u16 action}"},
        {"CCC8:000A", "move_table", "146 x {u16 move_id, u16 mode, u16 device, u16 bits}"},
        {"D843:0B33", "time_map_options", "8-byte {key=completed|1<<(bonus+8), still_start, still_stop, chapter(-1 trader)}"},
        {"D372:0060", "chapter_slot_weights", "slot x chapter % table, index 2*(slot*5+chapter)"},
        {"CD12:0002", "episode_table", "368 x 7 u16; ends when +6 == 0xFFFF; fields undecoded"},
        {"DBE1:0000", "dgroup_rom_image", "initial DGROUP; CRT copies 0x1C90 bytes to RAM 01EB:0000"},
        {"DBE1:117A", "specobj_table_rom", "28 x {u16 key, u16 audio_mask, u16 start, u16 stop}, key 0 terminates"},
        {"DBE1:0D24", "s_ld_cmds_rom", "Pioneer ASCII / Sony byte command strings"},
    };

    @Override
    public void run() throws Exception {
        int ok = 0, bad = 0;
        for (String[] f : FUNCS) {
            Address a = toAddr(f[0]);
            if (a == null) { printerr("bad address " + f[0]); bad++; continue; }
            disassemble(a);
            Function fn = getFunctionAt(a);
            if (fn == null) fn = createFunction(a, f[1]);
            if (fn != null) fn.setName(f[1], SourceType.USER_DEFINED);
            else createLabel(a, f[1], true);
            if (!f[2].isEmpty()) setPlateComment(a, f[2]);
            ok++;
        }
        for (String[] d : DATA) {
            Address a = toAddr(d[0]);
            if (a == null) { printerr("bad address " + d[0]); bad++; continue; }
            createLabel(a, d[1], true);
            setPlateComment(a, d[2]);
            ok++;
        }
        println("TimeTravelerLabels: applied " + ok + ", failed " + bad);
    }
}
