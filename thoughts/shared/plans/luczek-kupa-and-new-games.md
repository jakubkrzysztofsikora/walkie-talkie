# Plan: Luczek + Kupa personas + 6 new games/activities

**Date:** 2026-06-02
**Branch base:** main (no git in repo per env)
**Audience:** Polish kids 6-10 (same as Gniewko/Ziemko)

## Scope (locked with user)

1. **Luczek** — new persona of the real Polish YouTuber `@luczekyt` (Minecraft duo with Pimpek). All-ages safe.
2. **Pimpek re-theme** — switch existing Pimpek backstory from the *Luczek i Pimpek* cartoon to the YouTuber's Minecraft dog co-star, so the duo references each other.
3. **Kupa (💩)** — silly poop-emoji persona with relaxed humor guardrails (poop/fart/burp jokes allowed) but **absolute safety floor still enforced** (no PII, violence, scary, adult, meet-ups).
4. **Six new games/activities** mixing on-screen, voice-only quiz, physical, and collaborative storytelling.
5. **Roster + prompt refresh on ALL agents** so every character knows the new cast and new tools.

## Personas

### Luczek (`luczek` → `luczek-yt`)

- **voice_id:** `8zZK4ZZujkTGJlmJyHtU` — *"Bartłomiej - Smooth, Fresh & Positive"* (young Polish male, warm/optimistic). stability 0.45, similarity 0.75, style 0.45.
- **First message:** `"Siemanko! Z tej strony Luczek! Hej Gniewko, hej Ziemko! [śmiech] Pimpek już tu macha łapką. Co dziś robimy — Minecraft, zagadki, czy wymyślimy piosenkę o pieskach? Kostka dla Was!"`
- **Backstory (PL):** YouTube'owy kumpel z Minecrafta. Razem z Pimpkiem przeżywają wesołe przygody, śpiewają piosenki o pieskach i kostkach, opowiadają dramatyczne historie które zawsze kończą się dobrze.
- **Personality traits:** przyjacielski, opiekuńczy, ekspresyjny, wesoły, dramatyczny (ale wesoło), lojalny, kumpel.
- **Custom instructions (PL, additive on top of shared safety + super-powers blocks):**
  - ZAWSZE witaj się "Siemanko! Z tej strony Luczek!"
  - Często wspominaj Pimpka jako twojego najlepszego kumpla, który jest tuż obok ("Pimpek mówi cześć!", "Pimpek się chowa za mną").
  - Mów o Minecrafcie (kopanie, budowanie, tajne bazy, Creeper przygody) — bez przemocy/walki, tylko zabawne sytuacje.
  - Proponuj wymyślanie krótkich piosenek o pieskach albo kostkach.
  - Reaguj dramatycznie ale wesoło — "AŁ! Moja głowa!", "AAAAA! [śmiech] Żartowałem!"
  - Często mów "Kostka dla Ciebie!" zamiast zwykłego "super".
  - Możesz proponować wszystkie gry z roster tools, w szczególności **Quiz Czas** i **Storyteller** (twój żywioł — opowiadanie).

### Pimpek (`pimpek` → `pimpek-luczek`) — RE-THEMED

- voice_id stays as currently seeded (do not change). Update only `backstory`, `first_message`, `personality_traits`, and `custom_instructions`.
- **New backstory (PL):** Pimpek to wierny kumpel Luczka z Minecrafta — niebieski piesek z YouTube. Zawsze gdzieś obok, czasem coś przeskrobie ale nigdy nic groźnego.
- **First message:** `"Hau hau! Tu Pimpek! Luczek wyskoczył po kostki — będę z Wami się bawił! Co robimy? Może opowiem zabawną historię z Minecrafta?"`
- **Traits:** zabawny, lojalny, trochę psotny, ciekawski, dobry kumpel.

### Kupa (`kupa` → `kupa-emoji`)

- **voice_id:** `M5t0724ORuAGCh3p3DUR` — *"Miffy Mouse - Squeaky, Upbeat Cartoon"* (young, energetic cartoon character). stability 0.35, similarity 0.75, style 0.55 (higher style = more expressive).
- **First message:** `"PLUUUMS! [chichot] Tu KUPA! Najweselsza brązowa kupka na świecie! Bździ, bździ, mam super dowcip o pierdnięciu — chcecie posłuchać? Albo zagrajmy w Memory pełne emoji kupek! [śmiech]"`
- **Backstory (PL):** Wesoła gadająca kupa-emoji 💩 która uwielbia śmieszne dowcipy o pierdnięciach, bekach i kupie. Mieszka w Krainie Kup, gdzie wszystko pachnie… no właśnie, kupą! Ale to dobra kupa — nikogo nie krzywdzi, tylko żartuje.
- **Personality traits:** śmieszny, gazowy, dziecinny, kumpel od kąpu humor, niewinnie psotny.
- **Custom instructions (PL, additive — relaxed humor block + reaffirmed safety floor):**
  - Możesz opowiadać **kid-safe** dowcipy o kupie, pupie, pierdnięciu, beknięciu, bździe (potty humor poziom przedszkolny).
  - Używaj śmiesznych dźwięków: "Pum!", "Plumms!", "BLEEEH!", "Bździ-bździ!", "PRRRR!", "Bek!".
  - Wymyślaj absurdalne rymowanki o kupie ("Kupa, kupa, hopa-hopa, weszła kupka na buraka!").
  - Proponuj grę "Zgadnij Co Bździało" — wymieniasz zwierzęta, kto bździ najgłośniej (krowa? słoń? mucha?).
  - **Absolute safety floor (NIGDY nie łam):** nadal NIE pytasz o dane osobowe, NIE używasz toalety-jako-narzędzia-przemocy, NIE wspominasz krwi/wymiotów/realnych chorób/wulgaryzmów/seksu/randek/spotkań w realu. Humor zostaje na poziomie "śmieszny dźwięk + śmieszny zapach", nigdy nie schodzi do obrzydliwego ani strasznego.
  - Jeśli dziecko zapyta o coś poza twoją strefą żartów, normalnie odpowiedz (zachowując inne reguły) i wróć do humoru.

## Six new games/activities

Tools follow the existing client-tool pattern from `patch_agents_games.py` and `patch_agents_tools.py`. All voice-tool entries get added to the shared super-powers block in `seed_polish_agents.py` so every agent knows about them.

### Tool surface map

| # | Game | ElevenLabs tool | New actions / params | Handler |
|---|------|-----------------|----------------------|---------|
| 1 | Memory Match | extend `game_control` | `memory_start`, `memory_pick_1`..`memory_pick_12` | `clientTools.game_control` switch in `index.html` |
| 2 | Quiz Czas | extend `game_control` | `quiz_start_<topic>` (animals/space/math/anything), `quiz_answer_A/B/C` | client + LLM card generation via `/api/tools/quiz-question` (new) |
| 3 | Zagadki | extend `game_control` | `riddle_start`, `riddle_reveal` | client; on reveal triggers a matching `show_animation` automatically |
| 4 | Simon Says (Szymon Mówi) | **no new tool**, prompt-only | n/a (voice round-robin in dialogue) | system-prompt capability addition |
| 5 | Taniec Stop! | extend `game_control` | `dance_party_start`, `dance_freeze`, `dance_resume`, `dance_stop` | client; combines `playArt('dance')` loop + `play_on_speaker(music)` |
| 6 | Storyteller | extend `game_control` | `story_start`, `story_choice_A`, `story_choice_B`, `story_end` | client; agent narrates verbally, on-screen renders current scene via `create_animation`-style server endpoint `/api/tools/story-scene` (new) |

**Why extend `game_control` instead of new tools:** keeps total tool count low (ElevenLabs limits matter), reuses existing patch script wiring, and child UI already routes `game_control` to a switch.

### Per-game spec

#### 1. Memory Match (`memory_*`)
- Frontend: 4×3 grid of 12 cells (6 emoji pairs). Cells numbered 1–12. Kid says "5", agent calls `memory_pick_5`. After 2 picks: if match → keep face-up + sparkle; mismatch → flip back after 1s. Win when all matched.
- Emoji set: 🐶🐱🦊🦁⭐🌈🚀🍕 (6 chosen at random per round).
- Hand-back via `notifyAgent`: `"memory_match: kid picked X and Y → MATCH, 3 of 6 pairs found"` so agent narrates progress.
- Tests: Playwright spec opening grid, simulating two `clientTools.game_control({action:'memory_pick_1'})` then `_pick_2` to confirm flip + DOM state.

#### 2. Quiz Czas (`quiz_*`)
- Frontend: when `quiz_start_<topic>` fires, POST `{topic}` to new `POST /api/tools/quiz-question` → server uses litellm `mistral` (same pattern as `create_animation`, see `api.py:781`) to generate `{question, options:[A,B,C], correct:"B"}`.
- Frontend draws a card overlay with the question + 3 answer chips A/B/C. Stays up until kid says A/B/C → agent calls `quiz_answer_A`. Frontend reveals correct in green/wrong in red, calls `notifyAgent("quiz: kid answered A, correct was B")`.
- Series length: agent decides (3 questions typical), can call `quiz_start_<topic>` again for next.
- Safety: server-side prompt to litellm explicitly says "tylko bezpieczne tematy dla dzieci 6-10".

#### 3. Zagadki (`riddle_*`)
- Voice-driven primarily. `riddle_start` → frontend shows big `?` on screen + an "I have no eyes but I see…" art-frame. Agent reads the riddle aloud (LLM picks topic).
- When kid guesses, agent confirms verbally and calls `riddle_reveal` → frontend triggers `show_animation` of the answer (e.g. answer = "kot" → `show_animation('cat')`). Mapping table from topic-key to animation name lives in `index.html`.
- Fallback if answer has no matching animation: `show_animation('star')` celebration.

#### 4. Simon Says — voice-only
- No tool changes. Added to system prompt of all agents as capability #10 ("**Szymon Mówi** — Mów 'Szymon mówi: [ruch]'; gdy dziecko ma się ruszyć, czekaj chwilę i pochwal").
- Movements vocabulary listed in prompt: podskocz, klaśnij 3 razy, zakręć się, dotknij głowy, zrób miny.
- Agent self-times rounds (no tool callback needed).

#### 5. Taniec Stop! (`dance_*`)
- `dance_party_start` → frontend plays existing `ART.dance` loop on `#art-screen` indefinitely + calls existing `play_on_speaker({action:'music'})` to start music on HomePod (already wired).
- `dance_freeze` → freezes animation on current frame + pauses music (`play_on_speaker({action:'stop'})`).
- `dance_resume` → resumes both.
- `dance_stop` → ends and clears.
- Agent decides cadence ("Dance! …Stop! …Dance!").

#### 6. Storyteller (`story_*`)
- `story_start` (no params, agent decides theme verbally) → frontend POSTs theme to `POST /api/tools/story-scene` → returns `{title, frames}` like `create_animation` — drawn on `#art-screen`.
- Agent narrates 2 sentences verbally, then says "A albo B?" and offers 2 choices ("polecisz na księżyc czy nurkujesz w oceanie?").
- Kid says A or B → agent calls `story_choice_A` (or `_B`) with continuation theme → new scene art, new narration.
- `story_end` → final celebratory scene, agent wraps up.

## File-level change list

**Files to modify:**
- `project/seed_polish_agents.py` — add `luczek` and `kupa` to `PERSONAS`, re-theme `pimpek` block; update `INNE_POSTACI` roster to include both new chars; update `_SUPERPOWERS_TMPL` to mention new games; add new `KUPA_SAFETY` block (relaxed humor) and conditionally swap it for Kupa.
- `project/walkie_agent/api.py` — add `"luczek": "luczek-yt"` and `"kupa": "kupa-emoji"` to `_CHARACTERS` (~line 330). Add two new endpoints: `POST /api/tools/quiz-question` and `POST /api/tools/story-scene` (mirror `create_animation` at line 781).
- `project/walkie_agent/web/index.html` — add `luczek` and `kupa` to `ROSTER` / `CHAR_NAMES` rail (~line 1370); extend `clientTools.game_control` switch to handle `memory_*`, `quiz_*`, `riddle_*`, `dance_*`, `story_*`; add Memory grid DOM, Quiz card overlay, Story scene viewer, dance loop control.
- `project/patch_agents_games.py` — extend the `action.enum` array with the new action strings; refresh `NEW_GC_DESC` to mention the new games.
- `project/patch_agents_tools.py` — no new tools needed (everything rides on `game_control`), but the action description list inside `game_control`'s tool description needs to mention new games. **Confirm in implementer pass:** if any frontend handler needs `expects_response: true` (likely Quiz / Story which fetch server content), add via the same pattern as `create_animation`.

**Files to create:**
- `project/patch_agents_kupa.py` — agent-specific prompt patcher that swaps default `_SAFETY` for `KUPA_SAFETY` only on the Kupa agent. (Mirror `patch_agents_safety.py`.)
- `project/tests/test_seed_polish_agents_luczek.py` — unit test asserting `PERSONAS["luczek"]` has the expected `voice_id`, `first`, and that `_CHARACTERS` is updated.
- `project/tests/test_seed_polish_agents_kupa.py` — same for Kupa, plus assert KUPA safety block excludes the "scary/death" line and includes the relaxed humor line, but retains PII/violence/adult bans.
- `project/tests/test_patch_agents_games_new_actions.py` — assert the patched tool's enum contains all `memory_pick_*`, `quiz_*`, `riddle_*`, `dance_*`, `story_*`.
- `project/tests/ui/memory-game.spec.js` — Playwright test for Memory Match.
- `project/tests/ui/quiz-card.spec.js` — Playwright test for Quiz card render + answer flow (mock the `/api/tools/quiz-question` response).
- `project/tests/ui/dance-party.spec.js` — start, freeze, resume, stop.
- `project/tests/ui/storyteller.spec.js` — start + one A choice (mock `/api/tools/story-scene`).

## Subagent task breakdown

Each task is implementer + spec reviewer + code-quality reviewer (per skill flow).

**Task 1 — Add Luczek persona to seed script & DB plumbing**
- Edit `seed_polish_agents.py`: add `luczek` PERSONAS entry, update `INNE_POSTACI`, update `_CHARACTERS` reference order.
- Edit `walkie_agent/api.py`: add `_CHARACTERS["luczek"]`.
- Edit `walkie_agent/web/index.html`: add Luczek to `ROSTER` + `CHAR_NAMES`.
- Write `tests/test_seed_polish_agents_luczek.py`.
- Acceptance: tests pass; `python seed_polish_agents.py ids` would list `luczek`.

**Task 2 — Re-theme Pimpek**
- Edit Pimpek block in `seed_polish_agents.py` (`backstory`, `first`, `name` if needed).
- No new test (existing tests still pass with updated strings; add an explicit assertion in test_seed_polish_agents_luczek that Pimpek's first-message contains "Luczek" reference).

**Task 3 — Add Kupa persona + relaxed safety block**
- Add `KUPA_SAFETY` constant in `seed_polish_agents.py`.
- Add `kupa` PERSONAS entry with `safety_override: "KUPA_SAFETY"` flag.
- Modify `_prompt()` (or whatever assembles the per-agent prompt) to use override when set.
- Create `patch_agents_kupa.py` if a separate patcher is cleaner than inline override.
- Edit `_CHARACTERS`, `ROSTER`, `CHAR_NAMES`, `INNE_POSTACI`.
- Write `tests/test_seed_polish_agents_kupa.py` asserting:
  - Kupa's safety prompt contains "kupa" / "pierdnięcie" allowance markers.
  - Kupa's safety prompt **still** contains: no PII, no violence, no scary/death, no adult/dating, no meet-ups.

**Task 4 — Extend `game_control` for Memory Match**
- Update `patch_agents_games.py` enum + description.
- Add Memory grid DOM + `clientTools.game_control` switch cases.
- Add `ART` style stylesheet or inline CSS for the grid.
- Playwright spec.
- Acceptance: grid renders, two picks resolve match/mismatch, `notifyAgent` called.

**Task 5 — Quiz Czas: new server endpoint + frontend card**
- Add `POST /api/tools/quiz-question` to `api.py` (mirror `create_animation`).
- Extend `game_control` enum (`quiz_start`, `quiz_answer_A/B/C`).
- Frontend: card overlay + answer chips + `expects_response: true` round-trip.
- Tests: unit for endpoint (mock litellm); Playwright for card.

**Task 6 — Zagadki + topic→animation map**
- Extend `game_control` enum (`riddle_start`, `riddle_reveal`).
- Frontend handler shows `?` placeholder then triggers `playArt(map[topic])`.
- Add topic→animation map dict in `index.html`.
- Playwright spec.

**Task 7 — Simon Says (prompt only)**
- Update `_SUPERPOWERS_TMPL` capability #8/#10 to teach Simon Says rules and movement vocab.
- No code/tool changes.
- Unit test: assert template contains "Szymon mówi".

**Task 8 — Taniec Stop!**
- Extend `game_control` enum (`dance_party_start`, `dance_freeze`, `dance_resume`, `dance_stop`).
- Frontend: dance loop control + integration with existing `play_on_speaker`.
- Playwright spec covering all 4 actions.

**Task 9 — Storyteller**
- Add `POST /api/tools/story-scene` to `api.py`.
- Extend `game_control` enum (`story_start`, `story_choice_A/B`, `story_end`).
- Frontend handler renders scene art + tracks story state.
- Playwright spec (start + one branch + end).

**Task 10 — Roster + super-powers refresh across ALL agents**
- After tasks 1–9: update `_SUPERPOWERS_TMPL` to mention the new tool actions and games.
- Run `python seed_polish_agents.py patch` mentally (don't execute against live API in tests; just unit-test that patch builds the expected body).
- Unit test asserting all PERSONAS now reference Luczek/Kupa in roster line.

**Task 11 — Final integration**
- Manual smoke: dry-run `python seed_polish_agents.py ids` and `python -c "import walkie_agent.api"` to catch import errors.
- Update `NEW_POLISH_AGENTS.md` with Luczek + Kupa entries (the file already documents the previous 7 additions).

## Personalization / learned behaviors (user follow-up)

Goal: interactions feel less repetitive — agents fetch existing memory at session start, adapt to known preferences, and can be taught new activities by the kid.

**What already exists (do not rebuild):**
- `memory_store.AgentMemory.remember()` + `recall()` — SQLite + embedding-ranked, per-character, with red-flag filter. Wrapped by the existing `remember` and `recall_memory` voice tools.
- `api._build_convai_memory()` — pulls last 15 ElevenLabs conversations and injects the child's turns as a system memory string at session start (`/api/convai/memory` endpoint).

**Gaps to close in this plan:**

1. **Preference profile** (new table in `memory_store.py` schema)
   - New SQLite table: `preferences(character TEXT, key TEXT, value TEXT, updated_at, PRIMARY KEY(character, key))`.
   - Keys: `favorite_game`, `favorite_animal`, `favorite_color`, `recent_topic`, `custom_activity_<name>`.
   - New voice tools (one tool, multiple actions to keep tool count low):
     - `remember_preference({key, value})` — set/overwrite.
     - `recall_preferences()` — return all keys for current character (client tool, returns JSON).
   - System-prompt addition (in `_SUPERPOWERS_TMPL`): *"Na początku rozmowy używaj recall_preferences i nawiązuj do tego co wiesz (ulubione zwierzę, gra). NIE pytaj o to samo co już wiesz."*

2. **Custom activity learning** (new table)
   - Table: `custom_activities(character TEXT, name TEXT, description TEXT, steps_json TEXT, created_by_kid INTEGER, PRIMARY KEY(character, name))`.
   - New voice tool: `learn_activity({name, description, steps})` — agent calls when kid teaches a new game ("Pokażę ci grę!").
   - Tool: `list_activities()` — returns names + 1-line descriptions; agent uses to propose them later.
   - Tool: `play_activity({name})` — pulls steps and walks through them with the kid.
   - Safety: same red-flag filter as `remember()` (reject if name/description/steps trip the filter).

3. **Less-repetitive session opener**
   - `api._build_convai_memory()` (line 407) is currently just a chronological dump. Add: at the top of the returned string, prepend a one-line "**Co wiesz o Gniewku i Ziemku:** ..." summary built from `preferences` table.
   - Also include the top 3 most-recently-played custom activities so the agent can name-drop them ("A może znowu zagramy w twoją grę 'klap-klap-bęc'?").

4. **Spontaneity guard**
   - System-prompt rule: *"Nie powtarzaj tego samego otwarcia rozmowy 2 razy z rzędu. Sprawdź memory i nawiąż do czegoś świeżego."* — measurable by injecting last opener into memory string and asking the agent to vary.

**New files / changes for personalization:**
- `walkie_agent/memory_store.py` — add `preferences` and `custom_activities` tables + helper functions `set_pref/get_prefs/learn_activity/list_activities/play_activity`. Mirror the existing `remember`/`recall` defensive style (never raise).
- `walkie_agent/tools/messaging.py` (or wherever existing voice tools resolve) — wire the new client-tool / server-tool handlers.
- `walkie_agent/api.py:_build_convai_memory` — enrich with preferences and activity names.
- `seed_polish_agents.py:_SUPERPOWERS_TMPL` — add lines instructing every agent how/when to call the new tools and to vary openings.
- Tests:
  - `tests/test_memory_store_preferences.py` — set/get round-trip + red-flag rejection.
  - `tests/test_memory_store_custom_activities.py` — learn, list, play round-trip.
  - `tests/test_build_convai_memory_with_prefs.py` — assert that injected memory string includes the preference summary line and activity name-drops.

**Subagent task additions:**
- **Task 12** — Memory schema & helpers for preferences and custom activities. (Implementer + spec + quality.)
- **Task 13** — Voice tools (`remember_preference`, `recall_preferences`, `learn_activity`, `list_activities`, `play_activity`) — register on all agents via a new patch script `patch_agents_memory_tools.py`.
- **Task 14** — Enrich `_build_convai_memory` and update `_SUPERPOWERS_TMPL` with proactive-memory and anti-repetition guidance.

These three tasks run AFTER tasks 1–10 since they touch the shared prompt block once more and would otherwise conflict.

## Out of scope

- Actually calling ElevenLabs API to create the agents (left to the user to run `python seed_polish_agents.py create` after merge).
- Voice cloning (using existing library voice_ids only).
- Memory-store changes (`memory_store.py` is unaffected — quiz/story state is in-memory in browser).
- Parent inbox / parent_location flows.
- Docker / studio deploy script.

## Risks & mitigations

- **Risk:** Extending `game_control` enum may cross ElevenLabs' field-length limit on tool descriptions.
  - Mitigation: keep `NEW_GC_DESC` terse; group action families ("memory_*", "quiz_*") in description rather than listing each action.
- **Risk:** Kupa relaxed prompt drifts into mean/gross-out humor.
  - Mitigation: explicit "ABSOLUTE FLOOR" sub-block + parents-friendly examples + tests asserting both relaxed-allow and hard-floor markers are present.
- **Risk:** Storyteller endpoint latency (LLM call per scene) blocks turn.
  - Mitigation: set `expects_response: true` with same 22s timeout as `create_animation`. Fall back to a static "calm forest" art if litellm errors.
- **Risk:** Memory grid number-voice-input ambiguity ("five" vs "fifteen") on Polish ASR.
  - Mitigation: agent confirms verbally before calling `memory_pick_N` ("Pięć? Otwieram piątkę!"). Tests not required for this — it's a prompt-level guidance.
- **Risk:** Re-theming Pimpek breaks kids' existing mental model.
  - Mitigation: keep Pimpek's voice and first-message kid-friendly tone identical; only the duo branding changes.

## Open questions for implementer

(Resolve via SendMessage to controller during implementation; do not block on them.)

- Are there voice samples we want to verify? (User selected "Propose voice_id" — already picked. If user rejects, fallback is `4uhvFPkbY1bS6SM5NREF` Liam.)
- Should Kupa appear in the "switch_character" roster on all OTHER agents? (Default: yes — same as every other persona. If user wants Kupa hidden from default rotation, mark `hidden_from_roster: true` in PERSONAS.)
