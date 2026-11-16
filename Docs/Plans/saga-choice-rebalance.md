# Saga choice rebalance: no cut = tragedy, every cut changes the night and the ending

Goal: A run with no cuts ends in a coherent tragedy (the hall falls, or at best a "bitter dawn"), every cut removes one named doom from the final night at a real permanent price, and the skald's epilogue has six endings that name the player's cuts as the cause.
Out of scope: `TSCHUD.cpp` (this plan needs no HUD change; the HUD was just reworked in `8ef25bd`), unit AI / pathfinding (`TSCUnit.cpp`, `TSCNav.cpp`), buildings and their costs, new systems or UI, the prologue, `MaxEdits` (stays 3), the day-one onboarding flow, music.

## Context

- Only three files change: `UE/Source/TheSagaCutter/TSCGameMode.cpp`, `UE/Source/TheSagaCutter/TSCGameMode.h`, `UE/DevLoc/ru.tsv`. Line numbers below are for `TSCGameMode.cpp` at commit `8ef25bd` (the file has no uncommitted edits); re-find by the quoted snippet if they have moved.
- **Text pattern.** English strings are hardcoded `TEXT("...")`. Russian is `UE/DevLoc/ru.tsv`, one `English<TAB>Russian` per line, UTF-8 **with BOM**, LF, key = the exact English string as drawn. Translation happens at draw time, so `Toast(TEXT(...))`, `ShowBanner`, scene fields and `Epilogue.Add(TEXT(...))` need no `Tr()` call. Only strings that are concatenated in C++ are wrapped in `TSCLoc::Tr(...)` (see `StartNight` line 2454 and `BuildEpilogue` line 2736). Keys with fixed digits are written literally (`+3 food income`); strings built with `%d` use `#` per digit run in the key and `{0}`, `{1}` in the value. Lines without a TAB are comments. See `UE/DevLoc/README.md`.
- Russian glossary already in the table: cut = «склейка», CUT! = «СНЯТО!», `EDIT:` = «МОНТАЖ:», fog-born = «порождения тумана», longhouse = «длинный дом», hearth = «очаг», food = «пища», wood = «древесина». The skald is «скальда» (existing line 187 of ru.tsv).
- **Autoplay.** `TSCAuto <takes> <build> <speed>`: one `L`/`A` per scene (0 boar, 1 spruce, 2 grove, 3 dock); build letters `H` house, `M` sawmill, `E` healer's hut, `S` Hammer, `T` Stag; unknown letters are skipped. Use `X` for "no builds". **`NONE` is not "none": its `E` builds a healer's hut.** Autoplay never touches jobs (4 wood / 4 food / 2 idle) and turns the onboarding off. With `MaxEdits = 3` a fourth `A` is ignored.
- Combat is a live simulation with random attack timing, so the same pattern gives different loss counts. This plan therefore makes the scene consequences deterministic (who lives, wave sizes, hall strength) and classifies the ending partly from deterministic state, so a no-cut run can never get a good title whatever the fight does.
- Epilogue panel height is fixed (`ATSCHUD::DrawEpilogue`); keep the epilogue at the current 10 lines (11 when hungry). The take cards (`TSCHUD.cpp:871-873`) wrap text in a fixed box; scene texts below are kept at or under about 225 characters (the current longest is 215). The boar scene uses a smaller "first cut" card, so its two texts stay short.
- **Recent changes since the logs were taken** (`8586f1d`, `8ef25bd`): a cattle pen now stands in `BuildWorld` at cells 21..25 x 21..24 with the ring of cells around it blocked for pathing, and the cows/bull graze inside it; the HUD (incl. epilogue and take-card drawing) was reworked and committed. Neither touches the scene, economy or night code, but the pen can slightly change paths across the south-east of the village, so the "Before" combat numbers are a guide, not a baseline — S6 measures the real ones.
- Build: `sh Scripts/build_editor.sh` from the repo root (builds `TheSagaCutterEditor Win64 Development`; **close every editor first**; prints only error / warning / Result lines).

### Diagnosis (measured from existing autoplay logs in `%LOCALAPPDATA%/ue-mcp/logs`)

1. **The hall almost never falls.** `TSCAuto LLLL X` (no cuts, no builds): gate 350, "well fed", gate breaks, 2-4 of 12 fall, 8-10 survive, hearth burns, title "THE SAGA OF THE GREEN CLEARING". One hall loss in 30+ logged runs. 10 creatures + 6 raiders are too few, creatures need about 40 s to walk to the hall, and dawn at 85 s saves it.
2. **Ashen prices are fake.** Wood and food saturate: every logged run, including three Ashen takes, shows "200 wood braces the gate" and "well fed". With default jobs an all-Ashen run ends with more food (about 245) than an all-Living run (about 241), because the living named villagers and the boar loss outweigh the "-2 food income" prices. So a cut is free and not cutting only costs unnamed spearmen.
3. **Uncut scenes do not reach the night.** The spruce scene kills Hakon but changes nothing else; the boar scene has no lasting effect at all (the epilogue even says the soil "forgave them by next morning"); not cutting also makes scenes run longer, which gives extra economy ticks.
4. **The ending ignores who died.** The title depends only on the number of Ashen takes and on the hall. A no-cut run with three of four named villagers dead reads as a green, rich success.
5. Hidden modifiers (gate +80 for the Ashen forest) and a random "against all odds" Hakon survival blur cause and effect.

### New rules (one line per scene; all stated on the take cards)

| Scene | Living (no cut) | Ashen (cut) |
|---|---|---|
| 0 Boar | -30 food, all food income x0.5 for the rest of day 1. No night effect. | Garden yield x0.6 for good. Hall +120 on the final night (the stone wall). |
| 1 Spruce | Hakon always dies (the 6 scene creatures have aggro range 700, so they go for Hakon, not day workers; the scene ends only when Hakon is dead or after 45 s). +3 creatures in every night wave (Pack 3 → waves 6/6/7; S6: was +2, 5/5/6). +40 wood at dawn. | Hakon lives and shoots at the gate. Waves 3/3/4. Forest wood x0.5, no +40. |
| 2 Grove | Sigrun always dies: no healing unless a healer's hut is built (the hut heals but does not bind Torvald). Grove +3 food income. | Sigrun heals all night and binds Torvald's wound (he starts the night at 80% instead of 45%; only Sigrun binds, not the hut). Grove income 0. Stag's blessing weakened (as now). |
| 3 Dock | Torvald wounded (45%), -30 food, 3+3 = six raiders land (S6: was 4+3). Dock +3 food income. | Torvald whole, 2+1 slow raiders. Dock income 0. |

Other numbers: food upkeep 0.6 per villager per tick (was 0.5); economy ticks only while no scene is playing (exactly 81 ticks per game); gate = 150 + wood spent, up to 300 wood (was 190 + 0.8 x up to 200, +80 hidden); hall 300, or 420 with the Ashen boar wall (S6: was 360/480 in this plan, 420 before it); night 105 s (was 85); hungry below 100 food, well fed from 200 (was 20 / 110), fed bonus x1.15 (was 1.18).

Endings, checked in this order at the end of the night:
1. **COLD HEARTH**: the hall fell.
2. **BITTER DAWN** (new): the hall stands, but two or fewer of the four named are alive, or half or more of the villagers who started the night fell.
3. **LONG HEARTH** (new, the best one): all four named alive, whatever the number of cuts (review fix: checked before Ashen Hold, so four alive is always Long Hearth).
4. **ASHEN HOLD**: three Ashen takes (three named alive).
5. **GREEN CLEARING**: at most one cut (and three named alive).
6. **TWO TELLINGS**: the rest (two cuts, three named alive).

A no-cut run always has Hakon and Sigrun dead, so it can only be Cold Hearth or Bitter Dawn.

### Outcome matrix (autoplay, default jobs, build `X`)

"Before" is from logs. "After" is the final state measured by the review-fix rerun (autoplay, build `X` unless noted; NIGHT line → ending per run).

| Takes | Before | After: NIGHT line | After: measured endings |
|---|---|---|---|
| `LLLL` (no cuts) | hall stands, 8-10 of 12 survive, Green Clearing | gate 450, hall 300, food 277-328, fed, creatures 6/6/7, raiders 3/3, no bow, no healer, Torvald 45%, villagers 9-10 (not 12: 2-3 day deaths remain) | x5: Bitter 3, Cold 2; named 0, lost 8 every run |
| `LAAL` | 12-14 of 14 survive, Two Tellings | hall 300, food 180, creatures 3/3/4, raiders 3/3, villagers 14, Torvald bound 80% | x3: Long 2, Two 1 (lost 0-1, gate held) |
| `LAAL E` (healer's hut) | — | as `LAAL` | x2: Long 2 |
| `AAAL` | 14 of 14 survive, Ashen Hold | hall 420, food 179, creatures 3/3/4, raiders 3/3, villagers 14 | x3: Long 2, Bitter 1 (named 2, lost 3) |
| `LAAA` | not logged | hall 300, food 157, creatures 3/3/4, raiders 2/1, villagers 14 | x3: Long 3 (lost 0) |
| `AALL` | not logged | hall 420, food 334, fed, creatures 3/3/4, raiders 3/3, villagers 13 | x3: Bitter 3 (lost 5-6, named 1-2) |
| `LALL` | not logged | hall 300, food 335, fed, creatures 3/3/4, raiders 3/3, villagers 13 | x3: Bitter 3 (lost 3-6, named 2, gate held) |
| `LLAL` | not logged | hall 300, food 72-173 (hungry in 2), creatures 6/6/7, raiders 3/3, villagers 9-13 | x3: Bitter 3 |
| `LALA` | not logged | hall 300, food 311, fed, creatures 3/3/4, raiders 2/1, villagers 13 | x3: Two 3 (lost 1) |
| `LLAA` | not logged | hall 300, food 98-149 (hungry in 2), creatures 6/6/7, raiders 2/1, villagers 10-11 | x3: Bitter 2, Two 1 |
| `ALLA` | not logged | hall 420, food 277-297, fed, creatures 6/6/7, raiders 2/1, villagers 9-10 | x2: Bitter 2 |

No run gave Ashen Hold or Green Clearing: with Long Hearth checked first, Ashen Hold needs three cuts with exactly three named alive, and Green Clearing needs at most one cut with three named alive, which no measured pattern reached (a single cut leaves Hakon or Sigrun dead, and the other named villagers often fall too).

## Steps

### S1 — Make the economy and the day scenes deterministic and costly   [routine]   deps: none ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.cpp`, `UE/Source/TheSagaCutter/TSCGameMode.h`
Do:
- `TSCGameMode.h:416-417`: `float GroveBonus = 3.f;` and `float DockBonus = 3.f;` (were `2.f`).
- `ComputeRates` (line 1771): change `- Living * 0.5f` to `- Living * 0.6f`.
- `SimDay` (line 1750): replace `EconomyTimer += Dt;` with `if (ActiveScene < 0) { EconomyTimer += Dt; }` (keep the `if (EconomyTimer >= EconomyInterval)` block as is).
- `ResolveLiving`, `Idx == 0` (lines 2176-2177): `Food - 30.f` and `DayFoodMul = 0.5f;`.
- `ApplyAshen`, `Idx == 0` (line 2256): `GardenMul = 0.6f;`.
- `ApplyAshen`, `Idx == 1` (line 2263): `ForestMul = 0.5f;`.
- `SimScene`, `Idx == 1`, creature spawn (line 2101): `C->MaxHP = C->HP = 75.f;`. Step 3 (line 2117): change `S.Timer > 80.f` to `S.Timer > 45.f`.
- `ResolveLiving`, `Idx == 1` (lines 2187-2195): replace the whole `if (Hunter->IsAlive()) {...} else {...}` with:
  ```cpp
  if (Hunter->IsAlive()) { Hunter->Kill(); }
  Toast(TEXT("Hakon the hunter falls outside the gate he could not reach. The fog-born have learned the way."));
  ```
  (The "Against all odds Hakon fights the fog-born off..." toast is deleted.)
- No other behaviour changes in this step. Do not touch `UpdateJobs` (`ForestMul >= 1.f` and `GroveBonus > 0.f` checks still work).
Accept: builds. `TSCAuto LLLL X 16` log shows `Scene 1 resolved: LIVING` preceded by the new Hakon toast in every run, and the night toast reports a food state of "well fed" (food about 254); `TSCAuto ALAA X 16`: the HUD food value at nightfall is roughly 70 (old thresholds still apply in this step).

### S2 — Rebalance the final night and announce what each scene did to it   [tricky]   deps: S1 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.cpp`, `UE/Source/TheSagaCutter/TSCGameMode.h`
Do:
- `TSCGameMode.h:118`: `float NightLength = 105.f;`. In the private "economy modifiers" block add `int32 CreatureWaves[3] = { 3, 3, 4 };` and `int32 RaiderWaves[2] = { 3, 3 };`.
- `StartNight` (lines 2401-2408), replace with:
  ```cpp
  const float Spent = FMath::Min(Wood, 300.f);
  Wood -= Spent;
  GateMaxHP = GateHP = 150.f + Spent;
  HallMaxHP = HallHP = 300.f + (Scenes[0].bAshen ? 120.f : 0.f);   // S6: 360 -> 300
  bHungryNight = Food < 100.f;
  bFedNight = Food >= 200.f;
  const float HpMul = bHungryNight ? 0.72f : (bFedNight ? 1.15f : 1.f);
  const float DmgMul = bHammerBlessing ? 1.3f : 1.f;
  ```
- `StartNight`, Torvald (lines 2425-2429): declare before the `if (Shield->IsAlive())` block
  `const bool bBound = Shield->IsAlive() && bShieldWounded && Healer->IsAlive();` (review fix: the healer's hut does not bind)
  and replace the inner `if (!bShieldWounded) {...}` with:
  ```cpp
  Shield->HP = Shield->MaxHP * (!bShieldWounded ? 1.f : (bBound ? 0.8f : 0.45f));
  ```
- `StartNight`, before the banner (after `EnemiesSlain = 0;`), set the waves:
  ```cpp
  const int32 Pack = Scenes[1].bAshen ? 0 : 3;                          // the fog-born that killed Hakon lead others in
  const int32 Calm = bStagBlessing ? (Scenes[2].bAshen ? 0 : 1) : 0;
  CreatureWaves[0] = CreatureWaves[1] = 3 + Pack - Calm;
  CreatureWaves[2] = 4 + Pack - (bStagBlessing ? 1 : 0);
  RaiderWaves[0] = Scenes[3].bAshen ? 2 : 3;   // S6: 4 -> 3
  RaiderWaves[1] = Scenes[3].bAshen ? 1 : 3;
  ```
- `StartNight`, the bracing toast (lines 2454-2456): append one more suffix `+ TSCLoc::Tr(Scenes[0].bAshen ? TEXT(" The old stone wall shields the longhouse door.") : TEXT(""))`. Then add three toasts, in this order:
  ```cpp
  Toast(Scenes[1].bAshen ? TEXT("Hakon's bow covers the west gate.")
      : TEXT("No bow answers at the west gate. The fog-born that killed Hakon lead the others in."));
  Toast(Healer->IsAlive() ? (bBound ? TEXT("Sigrun waits by the hearth with clean bandages. She has bound Torvald's wounds.") : TEXT("Sigrun waits by the hearth with clean bandages."))
      : (ExtraHealers > 0 ? TEXT("Sigrun is gone, but the healer's hut tends the wounded in her place.") : TEXT("No healer waits by the hearth. Every wound tonight stays open.")));
  Toast(Scenes[3].bAshen ? TEXT("Torvald stands whole on the empty shore. Only three raiders can wade in.")
      : TEXT("Six raiders pull for the dock. The scouts told them how few spears wait there."));
  ```
  and one log line for testing:
  ```cpp
  UE_LOG(LogSaga, Display, TEXT("[Saga] NIGHT gate=%d hall=%d food=%d hpmul=%.2f creatures=%d/%d/%d raiders=%d/%d villagers=%d"),
      FMath::RoundToInt(GateMaxHP), FMath::RoundToInt(HallMaxHP), FMath::FloorToInt(Food), HpMul,
      CreatureWaves[0], CreatureWaves[1], CreatureWaves[2], RaiderWaves[0], RaiderWaves[1], NightVillagersAtStart);
  ```
- `SimNight` (lines 2521-2523 and 2534-2536): delete `PerWave`, `Calm`, `LastCalm`; the three calls become `CreatureWave(CreatureWaves[0])`, `CreatureWave(CreatureWaves[1])`, `CreatureWave(CreatureWaves[2])`. Lines 2550-2551: `RaiderWave(RaiderWaves[0])` and `RaiderWave(RaiderWaves[1])`. Keep spawn times, positions, the slow-raider modifiers and all toasts.
- Do not change `DamageStructure`, heal rates, unit stats or `GetAssaultGoal`.
Accept (final numbers after S6 tuning): builds. `TSCAuto LLLL X 16` logs `[Saga] NIGHT gate=450 hall=300 food=25x hpmul=1.15 creatures=6/6/7 raiders=3/3 villagers=12`. `TSCAuto AAAL X 16` logs `gate=450 hall=420 ... hpmul=1.00 creatures=3/3/4 raiders=3/3 villagers=14`. `TSCAuto LAAA X 16` logs `raiders=2/1`.

### S3 — Six endings and an epilogue that names the cuts   [tricky]   deps: S2 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.cpp`
Do (all in `BuildEpilogue`, lines 2619-2738):
- After `bHakon...bTorvald` add `const int32 NamedAlive = (bHakon ? 1 : 0) + (bUlf ? 1 : 0) + (bSigrun ? 1 : 0) + (bTorvald ? 1 : 0);`, `const int32 CutsUsed = MaxEdits - EditsLeft;` and move `const int32 Lost = NightDefendersLost;` up here (delete the later declaration at line 2712).
- Replace the title block (lines 2627-2630) with a local `enum class EEnd { Cold, Bitter, Ashen, Long, Green, Two };` and:
  ```cpp
  const EEnd End = bHallFell ? EEnd::Cold
      : (NamedAlive <= 2 || Lost * 2 >= NightVillagersAtStart) ? EEnd::Bitter
      : NamedAlive == 4 ? EEnd::Long       // review fix: before Ashen
      : NumAshen >= 3 ? EEnd::Ashen
      : NumAshen <= 1 ? EEnd::Green : EEnd::Two;
  ```
  Titles: Cold `THE SAGA OF THE COLD HEARTH`, Bitter `THE SAGA OF THE BITTER DAWN`, Ashen `THE SAGA OF THE ASHEN HOLD`, Long `THE SAGA OF THE LONG HEARTH`, Green `THE SAGA OF THE GREEN CLEARING`, Two `THE SAGA OF TWO TELLINGS`.
- Opening line (2632): `CutsUsed == 0 ? TEXT("Hear me, the skald. You held the knife and never cut, so I sing the saga as it was written.") : TEXT("Hear me, the skald, for I sing the saga you cut.")`.
- Garden, Ashen branch (2636): `"When the boar came, you gave the garden a wall of old burnt stone. Not a cabbage was lost, the thin grey soil gave a little less ever after, and on the last night that wall stood before the longhouse door."` Living branch unchanged.
- Gate, Living branch (2646-2651): replace the two-way `bHakon ? ... : ...` with the single line `"The spruce fell across the gate and you let it lie. Hakon the hunter died on the wrong side of it, and on the last night the pale things that killed him came back and brought others."` Ashen branch unchanged.
- Grove and dock blocks, "what was built", hungry line and the three loss lines: unchanged, except both `FMath::Min(StartNightWood, 200.f)` (2704, 2708) become `FMath::Min(StartNightWood, 300.f)`.
- `Became` (2727-2731), by `End`:
  - Cold, `CutsUsed == 0`: `"You never once called CUT! The saga ran as it was written, and it was written as a lament: the clearing is a place of cold stones now."`
  - Cold, `CutsUsed >= MaxEdits` (review fix): `"Even three cuts could not hold back the night. So the clearing is a place of cold stones now, and this cut of the saga ends here."`
  - Cold, 1-2 cuts: `"Your cuts were too few, or fell on the wrong scenes. So the clearing is a place of cold stones now, and this cut of the saga ends here."`
  - Bitter: `"So the hearth still burns, but too few are left to sit around it, and no one sings a dawn like that as a victory."`
  - Long: `"So you cut only what had to be cut, and the land stayed half green and half ash. This is the telling the skalds will keep."`
  - Ashen, Green, Two: the existing three strings, unchanged.
- At the very end of `BuildEpilogue` add the test line:
  ```cpp
  static const TCHAR* EndNames[] = { TEXT("COLD_HEARTH"), TEXT("BITTER_DAWN"), TEXT("ASHEN_HOLD"), TEXT("LONG_HEARTH"), TEXT("GREEN_CLEARING"), TEXT("TWO_TELLINGS") };
  UE_LOG(LogSaga, Display, TEXT("[Saga] RESULT takes=%c%c%c%c cuts=%d ending=%s hallhp=%d gate=%s lost=%d survivors=%d named=%d hungry=%d"),
      Scenes[0].bAshen ? 'A' : 'L', Scenes[1].bAshen ? 'A' : 'L', Scenes[2].bAshen ? 'A' : 'L', Scenes[3].bAshen ? 'A' : 'L',
      CutsUsed, EndNames[(int32)End], FMath::RoundToInt(HallHP), bGateBroken ? TEXT("broken") : TEXT("held"), Lost, Survivors, NamedAlive, bHungryNight ? 1 : 0);
  ```
  (use `TEXT('A')` style chars if the compiler complains about `%c` with narrow chars.)
Accept: builds. Any autoplay run ends with one `[Saga] RESULT ...` line and still exactly 10 `[Epilogue]` lines (11 when hungry). `TSCAuto LLLL X 16` gives `cuts=0 ending=COLD_HEARTH` or `ending=BITTER_DAWN`, never another ending, and the first epilogue line is the "never cut" opening.

### S4 — Rewrite the take cards and scene toasts so they state the consequences   [routine]   deps: S3 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.cpp`
Do (`DefineScenes`, lines 1974-2017, plus two toasts). Titles, alerts and take names stay. New texts, exactly:
- Scene 0 `LivingText`: `Lose 30 food and half of today's harvest. The soil stays rich, but the longhouse gets no wall.`
- Scene 0 `AshenText`: `The wall saves the crops and shields the longhouse on the final night (+120 hall strength). Garden yields drop 40% for good.`
- Scene 1 `LivingText`: `Hakon dies alone outside the gate, and the fog-born that kill him learn the way: three more come in every wave on the final night. The spruce is sawn up at dawn (+40 wood) and the forest keeps giving timber.`
- Scene 1 `AshenText`: `Here the forest burned years ago. No spruce bars the gate, Hakon walks home, and his bow guards the west gate on the final night. But scorched stumps give no timber: no +40 wood, and forest wood is halved for good.`
- Scene 2 `LivingText`: `The spores take Sigrun before nightfall. On the final night no one heals your defenders unless you build a healer's hut. The grove keeps feeding the village (+3 food income).`
- Scene 2 `AshenText`: `Here the grove burned in the old war and nothing grows that could harm her. Sigrun lives and heals your defenders through the final night. But the glowing mushrooms are gone (-3 food income) and the Stag's blessing is weakened.`
- Scene 3 `LivingText`: `Torvald drives the scouts off but is badly wounded, and they carry away the stores (-30 food). Tonight six raiders land at the dock. The dock stands and the fishing goes on (+3 food income).`
- Scene 3 `AshenText`: `Here the dock burned with the fleet. The scouts row away, Torvald is unhurt, and tonight only three raiders wade ashore, slow in the surf. But the longship is a wreck and the fishing is gone (-3 food income).`
- `ResolveLiving`, boar toast (2181): `The boar tramples the garden: -30 food, and the harvest is halved for today.`
- `ApplyAshen`, boar toast (2258): `EDIT: the boar turns away from an old stone wall. The crops are safe and the wall will shield the longhouse, but the ash-poor soil will always yield less.`
Accept: builds. In PIE on `Map_Saga`, during the spruce scene `TSCCut 1 0` opens the cut screen and both cards show the new text without running past the card's bottom line ("No cut spent" / "Spends 1 cut. Permanent."). Check the boar card the same way in a normal (non-autoplay) start, where it uses the smaller first-cut layout.

### S5 — Bring `ru.tsv` in line with the new English strings   [routine]   deps: none (file is disjoint from S1-S4) ✅
Files: `UE/DevLoc/ru.tsv`
Do: keep BOM, LF and TAB separators. **Delete** the rows whose keys start with: `Lose 25 food to the boar`, `The wall saves the crops. Garden yields drop 25%`, `Hakon stands alone against the fog-born`, `Here the forest burned years ago. No spruce stood`, `The spores take Sigrun before nightfall. The grove`, `Here the grove burned in the old war and nothing grows that could harm her. Sigrun lives. But`, `Torvald drives the scouts off but is badly wounded, and they carry away the stores (-30 food). The dock`, `Here the dock burned with the fleet. The scouts find`, `The boar tramples the garden: -25 food`, `Against all odds Hakon`, `Hakon the hunter falls outside the gate he could not reach.`, `EDIT: the boar turns away from an old stone wall. The crops are safe, but`, `When the boar came, you gave the garden a wall`, `The spruce fell across the gate and you did not cut`, `The spruce fell across the gate and you let it lie.`. **Add** (in the matching sections; `→` stands for one TAB):
- `Lose 30 food and half of today's harvest. The soil stays rich, but the longhouse gets no wall.` → `Вепрь сожрёт 30 пищи и половину сегодняшнего урожая. Земля останется плодородной, но стены у длинного дома не будет.`
- `The wall saves the crops and shields the longhouse on the final night (+120 hall strength). Garden yields drop 40% for good.` → `Стена спасает посевы, а в последнюю ночь прикроет длинный дом (+120 к прочности). Но огород до конца саги даёт на 40% меньше.`
- Scene 1 Living → `Хакон гибнет один за воротами, а убившие его порождения тумана запоминают дорогу: в последнюю ночь в каждой волне их будет на троих больше. На рассвете ель распилят (+40 древесины), и лес по-прежнему даёт брёвна.`
- Scene 1 Ashen → `Здесь лес сгорел много лет назад. Ель не перекрывает ворота, Хакон возвращается домой, и в последнюю ночь его лук стережёт западные ворота. Но обгорелые пни не дают брёвен: нет +40 древесины, а лес навсегда даёт вдвое меньше.`
- Scene 2 Living → `Споры забирают Сигрун до темноты. В последнюю ночь твоих защитников некому лечить, если не построить хижину целителя. Роща по-прежнему кормит деревню (+3 к добыче пищи).`
- Scene 2 Ashen → `Здесь роща сгорела в старой войне, и не растёт ничего, что могло бы ей навредить. Сигрун жива и всю последнюю ночь лечит твоих защитников. Но светящихся грибов больше нет (-3 к добыче пищи), а благословение Оленя слабеет.`
- Scene 3 Living → `Торвальд отгоняет лазутчиков, но тяжело ранен, а запасы унесены (-30 пищи). Этой ночью у причала высадятся шестеро налётчиков. Причал цел, рыбалка продолжается (+3 к добыче пищи).`
- Scene 3 Ashen → `Здесь причал сгорел вместе с флотом. Лазутчики уплывают, Торвальд невредим, и этой ночью лишь трое налётчиков выберутся на берег, увязая в прибое. Но драккар разбит, а рыбалки больше нет (-3 к добыче пищи).`
- `The boar tramples the garden: -30 food, and the harvest is halved for today.` → `Вепрь топчет огород: -30 пищи, и сегодняшний урожай вдвое меньше.`
- `EDIT: the boar turns away from an old stone wall. The crops are safe and the wall will shield the longhouse, but the ash-poor soil will always yield less.` → `МОНТАЖ: вепрь сворачивает от старой каменной стены. Посевы целы, и стена ещё прикроет длинный дом, но скудная пепельная земля всегда будет родить меньше.`
- `Hakon the hunter falls outside the gate he could not reach. The fog-born have learned the way.` → `Хакон-охотник гибнет у ворот, до которых не смог дойти. Порождения тумана запомнили дорогу.`
- ` The old stone wall shields the longhouse door.` (leading space in key and value) → ` Старая каменная стена прикрывает дверь длинного дома.`
- `Hakon's bow covers the west gate.` → `Лук Хакона стережёт западные ворота.`
- `No bow answers at the west gate. The fog-born that killed Hakon lead the others in.` → `У западных ворот не отзывается ни один лук. Порождения тумана, убившие Хакона, ведут за собой остальных.`
- `Sigrun waits by the hearth with clean bandages.` → `Сигрун ждёт у очага с чистыми повязками.`
- `Sigrun waits by the hearth with clean bandages. She has bound Torvald's wounds.` → `Сигрун ждёт у очага с чистыми повязками. Она перевязала раны Торвальда.`
- `Sigrun is gone, but the healer's hut tends the wounded in her place.` → `Сигрун больше нет, но раненых вместо неё выхаживают в хижине целителя.`
- `No healer waits by the hearth. Every wound tonight stays open.` → `У очага не ждёт целитель. Этой ночью ни одну рану не перевяжут.`
- `Torvald stands whole on the empty shore. Only three raiders can wade in.` → `Торвальд стоит невредимый на пустом берегу. До него доберутся вброд лишь трое налётчиков.`
- `Six raiders pull for the dock. The scouts told them how few spears wait there.` → `Шестеро налётчиков гребут к причалу. Лазутчики рассказали им, как мало копий их там ждёт.`
- `THE SAGA OF THE BITTER DAWN` → `САГА О ГОРЬКОМ РАССВЕТЕ`
- `THE SAGA OF THE LONG HEARTH` → `САГА О ДОЛГОМ ОЧАГЕ`
- `Hear me, the skald. You held the knife and never cut, so I sing the saga as it was written.` → `Слушай меня, скальда. Ты держал нож и ни разу не сделал склейки, и я пою сагу такой, какой она была написана.`
- Garden Ashen epilogue line (S3) → `Когда пришёл вепрь, ты дал огороду стену из старого горелого камня. Ни один кочан не пропал, тощая серая земля с тех пор родила чуть меньше, а в последнюю ночь эта стена встала перед дверью длинного дома.`
- Gate Living epilogue line (S3) → `Ель упала поперёк ворот, и ты оставил её лежать. Хакон-охотник погиб по ту сторону, а в последнюю ночь убившие его бледные твари вернулись и привели других.`
- `You never once called CUT! The saga ran as it was written, and it was written as a lament: the clearing is a place of cold stones now.` → `Ты ни разу не крикнул «СНЯТО!». Сага шла, как была написана, а написана она была как плач: теперь поляна — место холодных камней.`
- `Your cuts were too few, or fell on the wrong scenes. So the clearing is a place of cold stones now, and this cut of the saga ends here.` → `Твоих склеек было слишком мало, или пришлись они не на те сцены. Так поляна стала местом холодных камней, и эта версия саги кончается здесь.`
- `So the hearth still burns, but too few are left to sit around it, and no one sings a dawn like that as a victory.` → `Так очаг ещё горит, но слишком мало осталось тех, кто сядет вокруг него, и такой рассвет никто не поёт как победу.`
- `So you cut only what had to be cut, and the land stayed half green and half ash. This is the telling the skalds will keep.` → `Так ты вырезал лишь то, что нужно было вырезать, и земля осталась наполовину зелёной, наполовину пепельной. Именно это сказание сохранят скальды.`
For the keys abbreviated above as "Scene N Living/Ashen" and "epilogue line (S3)", copy the English text character for character from S4 / S3.
Accept: file still starts with the BOM and has no CRLF. After S4: PIE, `TSCLang ru`, play `TSCAuto LLLL X 16` and `TSCAuto LAAL X 16` to the epilogue, run `TSCLocMisses`; `UE/Saved/DevLoc_missing.txt` lists none of the strings from this plan.

### S6 — Run the matrix and tune the night until the targets hold   [tricky]   deps: S1, S2, S3, S4, S5 ✅ (partial: LLLL Cold 2/5; see Review fixes)
Files: `UE/Source/TheSagaCutter/TSCGameMode.cpp`, `UE/DevLoc/ru.tsv` (only if a tuned number appears in a string)
Do: build, then run each pattern below 5 times (LLLL and LAAL) or 3 times (the rest) and collect the `[Saga] NIGHT` and `[Saga] RESULT` lines. Standalone runner (derived from `.claude/skills/ue-demo-video/scripts/playthrough.sh`, not yet tried in this exact form; run from the repo root in Git Bash, one run at a time):
```bash
UE="/c/Programs/UE_5.9/Engine/Binaries/Win64/UnrealEditor.exe"; PRJ="$(cygpath -w "$PWD/UE/TheSagaCutter.uproject")"
run() { LOG="$PWD/UE/Saved/Logs/sagabal_$1_$2_$3.log"; rm -f "$LOG"
  MSYS_NO_PATHCONV=1 "$UE" "$PRJ" /Game/Vikings/Maps/Map_Saga -game -windowed -ResX=640 -ResY=360 -nosound -nosplash -benchmark -fps=30 \
    -ExecCmds="TSCAuto $1 $2 16" -abslog="$(cygpath -w "$LOG")" >/dev/null 2>&1 &
  for i in $(seq 1 120); do sleep 5; grep -q "\[Saga\] RESULT" "$LOG" 2>/dev/null && break; done
  grep -h "\[Saga\] NIGHT\|\[Saga\] RESULT" "$LOG" | sed 's/.*\[Saga\] //'
  powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { \$_.CommandLine -like '*sagabal_*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }"; }
for n in 1 2 3 4 5; do run LLLL X $n; run LAAL X $n; done
```
Alternative through the editor: `ue_start` (offscreen), `call_tool` `EditorToolset.EditorAppToolset` / `StartPIE` on `/Game/Vikings/Maps/Map_Saga`, console command `TSCAuto LLLL X 16` (unreal-mcp `RunConsoleCommand`), read `%LOCALAPPDATA%/ue-mcp/logs/ue_<port>.log`, `StopPIE` between runs, `ue_stop` at the end.
Targets: the "After" column of the outcome matrix. If a target fails, change **one** knob at a time, in this order, rebuild and rerun that pattern plus `LLLL` and `LAAL`:
- `LLLL` hall survives in more than 1 of 5 runs: (1) hall base `360.f` → `300.f`; (2) `Pack` `2` → `3` (then change "two more come in every wave" in the scene 1 Living text and its Russian row «на двоих больше» → "three more" / «на троих больше»); (3) `RaiderWaves[1]` Living `3` → `4` (then "seven" → "eight", «семеро» → «восьмеро» in the scene 3 Living text, the east toast and their Russian rows).
- `LAAL` loses the hall, or loses more than 3 in most runs: (1) bound Torvald `0.8f` → `1.f`; (2) `RaiderWaves[0]` Living `4` → `3` ("seven" → "six", «семеро» → «шестеро»); (3) hall base back up by 40.
- `LAAA` or `AAAL` loses more than 3: use the `LAAL` knobs (they are not hungry, so hunger is not the cause).
Do not change unit stats, heal rates, or the ending rules.
Accept: the table of collected `RESULT` lines matches the Final check below; final values of any tuned knob are written into this plan's "New rules" table.

## Final check

1. `sh Scripts/build_editor.sh` prints `Result: Succeeded` and no errors (editor closed). Done after the review fixes.
2. Autoplay (runner from S6), expected after the review fixes, with the measured result:
   - `LLLL X` x5: `cuts=0`, never better than `BITTER_DAWN`. **Holds** (Bitter 3, Cold 2). NIGHT `villagers=12` expected: **fails**, measured 9-10 (day deaths remain).
   - `AAAL X`, `LAAA X`, `LAAL X` x3 each: mostly `LONG_HEARTH`. **Holds** (Long 2/3, 3/3, 2/3).
   - `LAAL E` x2: `LONG_HEARTH`. **Holds** (2/2).
   - `GREEN_CLEARING` or `TWO_TELLINGS` appears somewhere. **Holds** for Two Tellings (LALA 3/3, LAAL 1, LLAA 1); Green Clearing never appeared.
   - Torvald is bound (80%, "She has bound Torvald's wounds.") only when Sigrun is alive: matches in every run with Sigrun alive; no run had Sigrun dead with a healer's hut and Torvald wounded, so the hut case is checked in code only.
3. One manual run without autoplay on `Map_Saga`: press nothing during the boar scene; the first-cut card and prompts behave as before; the night toasts name Hakon, Sigrun and the dock; the epilogue fits the panel in English and in Russian (`F10`). Not done yet.
4. `git status`: only `TSCGameMode.cpp`, `TSCGameMode.h`, `UE/DevLoc/ru.tsv` (and this plan) changed by this work; `TSCHUD.cpp` / `TSCHUD.h` carry separate user work and are untouched by it.

## Review fixes

Applied after S6, all approved by the user:
1. Ending order: Cold → Bitter → **Long** (`NamedAlive == 4`, any number of cuts) → Ashen → Green → Two. All four heroes alive is always Long Hearth.
2. Torvald binding: `bBound = Shield->IsAlive() && bShieldWounded && Healer->IsAlive()` (the healer's hut no longer binds), bound HP back to `0.8f` (S6 had 1.f). The hut toast is unchanged and does not claim binding.
3. Spruce scene: scene creatures `AggroRange = 700.f` (was 2600) so they go for Hakon, not day workers; the scene resolves only on `!Hunter->IsAlive() || S.Timer > 45.f` (the `Alive == 0` exit and its now-unused counter are removed), so Hakon cannot win the fight and then drop dead.
4. Texts (C++ and `ru.tsv`): scene 2 Living card names the healer's hut; hungry epilogue line "had run dry" → "had run thin" («почти опустели»); Cold ending with three cuts says "Even three cuts could not hold back the night. ..." (1-2 cuts keep "Your cuts were too few...", 0 cuts keep "You never once called CUT!..."); the orphan `ru.tsv` row "So the clearing is a place of cold stones now, ..." is deleted.
5. Rerun result: see the Outcome matrix. Open issue: no-cut runs still start the night with 9-10 villagers, not 12, so the spruce scene (or its leaving creatures) still kills 2-3 day workers.

- **Day deaths fixed (after the rerun):** the spruce scene creatures spawn with `bFights = false` and `AggroRange = 450`, and only start fighting once at their hold point next to Hakon (`AtHoldPos()` check in Step 3); leaving creatures get `bFights = false` like the Ashen branch. A `[Saga] DAYDEATH` log line in `OnUnitDied` names daytime deaths. Measured: `LLLL` now always starts the night with `villagers=12 food=247` and ended BITTER_DAWN ×3 (no Cold Hearth in these 3); `LLAL`/`LLAA` villagers=13 (hungry), `ALLA` 12, `LAAL` still LONG_HEARTH.

## Risks / open questions

- **Combat variance may miss the targets with the starting numbers** (they are estimates; nothing was simulated while planning). Default: follow the S6 knob order; accept "4 of 5" style bands rather than chasing 5 of 5.
- **Longer take texts may overflow the cards**, mostly in Russian and on the small first-cut card. Default: shorten the offending sentence in `TSCGameMode.cpp` and `ru.tsv` (drop the last clause first); do not edit `TSCHUD.cpp`.
- **The night now posts 4 toasts at once (limit 5)**; "Pale shapes drift..." at 5 s pushes the bracing toast out early. Default: accept. If it reads badly, merge the west and east toasts into the bracing toast as suffixes.
- **Pausing the economy during scenes** makes the resource numbers on the HUD stand still while workers keep moving. Default: keep it (scenes are short and it removes the reward for not cutting).
- **Food can be fixed by moving one worker to food** (one farmer is worth about 120 food over the game), so hunger only bites players who ignore jobs or burn both grove and dock. Default: accept for the jam; if hunger should bite harder, raise the hungry threshold from 100 to 130, nothing else.
- **A no-cut player who builds very well** (two houses, Hammer) may keep the hall. Default: that is intended; the result is still "Bitter Dawn", never a good ending.
- **`Hunter->Kill()` during the day** goes through `OnUnitDied` → `UpdateJobs()`; this already happens when the creatures kill him, so no new path. If `Kill()` does not call `OnUnitDied`, call `UpdateJobs()` after it.
- **Old strings in other docs** (`Docs/MegaJam`, skills) may quote "Green Clearing" as the no-cut ending. Default: leave them; not part of this change.
