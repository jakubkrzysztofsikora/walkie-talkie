---
title: Voice-only learning activities for Polish-speaking preschoolers (3–6 yo) on the walkie-talkie agent
date: 2026-06-01
status: spec / decision-ready
audience: jakub (product owner of walkie-talkie)
scope: research + design brief, not an implementation plan
---

# Voice-only learning activities for the walkie-talkie agent (3–6 yo, Polish)

This brief catalogues developmentally appropriate, voice-only learning mini-activities
we can ship inside the existing ElevenLabs Conversational AI character (Sonic, Mario,
Pikachu, Steve, Creeper, etc.). It then narrows the catalogue to a shortlist of
**6 activities**, designs each in detail (tool surface, dialogue, content lists,
failure handling, progression, memory), and lists the agent-prompt patches and any
new client-tool stubs required.

Everything in this document respects the existing product surface:

- Voice in, voice out, **Polish**, illiterate users.
- A minimal CRT-style ASCII overlay (`miniShow(title, body)` inside `#art-terminal`,
  visible in `project/walkie_agent/web/index.html` around L1201). Two-dimensional,
  static text panels — not an interactive canvas.
- An existing `game_control` tool with the enum
  `snake_start | left | right | up | down | stop | rps_start | rps_rock | rps_paper |
  rps_scissors | ttt_start | ttt_1..ttt_9`, plus `show_animation`, `create_animation`,
  `switch_character`, `go_to_sleep`, `play_on_speaker`, `recall_memory`, `remember`,
  `send_message`, `parent_location`.
- Round-trip latency of **~1–2 s** per agent turn (ASR → LLM → TTS).

Nothing in this brief touches `project/` — it is a specification for the next
iteration.

---

## 1. Goals & constraints

### 1.1 Hard constraints from the product

- **Voice-only**. No reading, no writing, no drag-and-drop, no on-screen
  buttons, no camera, no precise motor control. The child holds a session entirely
  through speech.
- **Polish first**. Polish phonotactics, Polish word frequency, Polish nursery
  tradition. English fallback only for language-agnostic content (counting,
  colours, animal sounds).
- **Latency tolerance ~1.5 s** per turn. Any rapid-fire game ("how fast can you
  say X") is out. Anything that tolerates a 1.5–2 s pause from the agent is in.
- **Screen is read-only ASCII**. Use it for state, score, the current word, a
  picture, a simple grid — *never* for input. Wide enough for ~24 chars at the
  large font; multi-line OK; the agent always speaks the screen content too,
  because young children cannot read.
- **5-minute cap per activity** before the agent should suggest something else
  (existing pattern — "po 5 min zaproponuj rozmowę z inną postacią").
- **No shame on errors**. The persona is a friendly cartoon character, not a
  teacher. Mistakes get reframed (puppet-makes-mistake, retry with a hint).

### 1.2 Developmental targets — what we can actually teach over voice

The Polish *podstawa programowa wychowania przedszkolnego* (MEN 2017, amended
2024)[^1][^2] organises preschool outcomes into four developmental areas. The
cognitive area (*obszar poznawczy*) includes language and pre-literacy outcomes
that are explicitly voice-trainable:

- **Słuch fonematyczny / świadomość fonologiczna** — syllable segmentation,
  rhyming, onset isolation, initial-sound identification. Polish curricula
  emphasise *sylabizowanie* (clapping out syllables) before phoneme work because
  Polish syllables are perceptually salient and phonemes such as /sz/ /cz/ /ż/
  are blurred for many 3–4-year-olds[^3][^4][^5].
- **Słownictwo i opis** — vocabulary, naming, describing, opposites.
- **Liczenie 1–10** — verbal counting (stable-order), one-to-one correspondence
  in stories and finger-rhymes.
- **Rozumienie tekstu** — narrative comprehension, sequencing (*najpierw, potem,
  na końcu*), recalling events.
- **Słuchanie i uwaga** — Simon-Says-style executive function, multi-step
  instruction following.
- **Tożsamość językowa i kulturowa** — Polish songs, rhymes, folk tales.

The UK *EYFS Communication & Language* ELG[^6] frames the same skills as
**Listening, Attention & Understanding** + **Speaking**. The US Head Start
ELOF[^7] splits *Language & Communication* from *Literacy*, with **LIT-A
Phonological Awareness** as the strongest pre-reading predictor and the goal
"recognises rhyming pairs, counts up to 3 syllables, understands onset rhyme".

In other words: rhyming + syllable clapping + first-sound + listening + counting
+ story-recall is the universal preschool stack and is exactly what voice does
well. Anything that needs sight-words, letter-recognition, or fine-motor output
is out of scope for *this* device.

### 1.3 Conversational-AI-specific constraints

Children's ASR is harder than adult ASR: WER on preschoolers is several times
worse than on adults, and 3–4-year-olds substitute or drop phonemes (r→l, sz→s)
that the model may transcribe wrong[^8][^9]. Practical implications:

- Don't grade *pronunciation*. Grade *intent*. If the kid says "kuń" instead of
  "koń", accept it.
- Prefer **closed-set, semantically distinguishable** answers (cat / dog / cow,
  red / blue / yellow) over open-set answers (any rhyme, any colour).
- For the "kid says a sound and the agent must compare it" pattern, route through
  the LLM with explicit tolerance instructions ("akceptuj zbliżone brzmienie,
  zapis fonetyczny zignoruj") rather than string equality.
- Whisper-style ASR transcribes Polish diacritics inconsistently. Match
  case-folded and diacritic-folded forms.
- An average preschooler tolerates an adult conversational gap of ~1–2 s; longer
  pauses break engagement[^10]. Our ~1.5 s round-trip is at the edge — keep
  agent turns short (the existing prompt already enforces 1–2 sentences) and
  avoid back-to-back tool calls that would compound latency.

### 1.4 Dyslexia-friendliness & inclusivity

Drawing from the International Dyslexia Association[^11] and Reading Rockets[^12]:

- All preschool phoneme work should be **oral** and **playful**, never on paper.
- Errors must be reframed: the agent says the right form back without "no,
  that's wrong". Pattern: kid says "kuń" → agent says "**Koń!** Brawo, koń tak
  robi: i-haa! [śmiech]". The reformulation is the correction.
- Multi-sensory cues (clap, stomp, jump) are usable over voice — we can ask the
  kid to clap, the agent doesn't have to hear it.
- Avoid r-initial words in the easy tier (Polish /r/ is acquired late, often
  ~5–6 yo).
- Avoid sz/ż/ć/ś/dź/dż minimal pairs in the easy tier.
- Let the kid win RPS / TTT / quizzes more than half the time — the existing
  RPS already does the "remis/wygrywasz" framing.

### 1.5 What we are NOT trying to do

- Not a curriculum-grade product. Not a substitute for a *zerówka* teacher.
- Not assessment. We don't track WER, accuracy, or scoreboards across kids.
- Not screen-mediated; ASCII overlay is decoration + state, not the game.

---

## 2. Candidate activity catalogue

20 candidate voice-only mini-activities. Each one is one paragraph. They are
loosely sorted by complexity. The shortlist (§3) picks the 6 strongest.

### A. Zgadnij głoskę (First-sound game)

- **EN / PL**: Initial-phoneme identification / **„Zgadnij, na jaką głoskę?"**
- **Goal**: świadomość fonologiczna — *isolation* sub-skill[^3][^11].
- **Loop**: Agent says "Słowo: **K-K-K-kot**. Na jaką głoskę zaczyna się kot?"
  → kid says "K!" → agent confirms and gives a next word.
- **Screen**: `miniShow("▌ NA JAKĄ GŁOSKĘ? ▐", "K  O  T\n\n→ K K K …")` — the
  letter is decorative; the kid hears it.
- **Why it works**: Reading Rockets[^12] recommends initial-sound work for
  4–5-year-olds; continuous sounds (m, s, f) are easier than stops (k, t, p);
  Polish *podstawa* lists "wyodrębnianie pierwszej głoski w wyrazie" as a
  *zerówka*-ready outcome.

### B. Klap sylaby (Syllable clapping)

- **EN / PL**: Syllable segmentation / **„Wyklaśnij sylaby"**
- **Goal**: sylabizowanie, słuch fonematyczny[^3][^4].
- **Loop**: Agent says "Słowo **ba-nan**. Ile sylab? Wyklaśnij i policz!" → kid
  claps (we don't need to hear it) and answers "Dwa!" → agent confirms.
- **Screen**: `miniShow("▌ SYLABY ▐", "BA - NAN\n\n👏 👏")` — bullets per syllable.
- **Why it works**: Polish syllables are perceptually salient and rhythmic;
  *logopeda nowicka*[^4] and Przedszkole Nr 9 Nowa Sól[^5] put this at the
  centre of Polish phonological readiness; clapping turns abstract sound into
  motor action even though we can't hear the clap.

### C. Co się rymuje? (Rhyming)

- **EN / PL**: Rhyme matching / **„Co się rymuje?"**
- **Goal**: rhyme awareness — earliest accessible phonological skill[^11][^12].
- **Loop**: Agent says "Kot — czy rymuje się z **lot** albo **mama**?" → kid
  says "Lot!" → agent confirms and praises.
- **Screen**: `miniShow("▌ RYMY ▐", "KOT ━ ?\n\n A) LOT\n B) MAMA")` — option
  labels are spoken; the kid says the *word*, not the letter.
- **Why it works**: Rhyme is acquired before phoneme isolation; pre-K children
  love nursery rhyme structure; classic Polish rhymes like *„a-a-a, kotki dwa"*
  are pre-cached cultural material[^13][^14].

### D. Kończ zdanie z rymowanki (Cloze rhyme)

- **EN / PL**: Rhyming cloze / **„Dopowiedz!"**
- **Goal**: rhyme + listening + cultural literacy.
- **Loop**: Agent recites a 2-line couplet pausing on the last word, kid fills
  it in. *„Idzie pani stuk, stuk, dziadek z laską ___?"* → kid: "puk-puk!"
- **Screen**: optional — could just be `miniShow("▌ DOPOWIEDZ ▐", "Idzie pani
  stuk, stuk\n…")`.
- **Why it works**: Reading Rockets[^12] explicitly recommends this for 3–5-yo;
  cultural rhymes from Przedszkole Hajnówka[^15] and Mamotoja[^16] are
  pre-existing curriculum.

### E. Co mówi zwierzę? (Animal sounds)

- **EN / PL**: Animal-sound naming / **„Co mówi…?"**
- **Goal**: vocabulary, dźwiękonaśladowstwo, articulation practice for the
  youngest (3–4 yo).
- **Loop**: Agent: "Co mówi krowa?" → kid: "Muu!" → agent: "Brawo! A kura?" →
  "Ko-ko-ko!". Reverses too: agent imitates, kid names.
- **Screen**: animal ASCII art (we already have cat/dog/fish via
  `show_animation`).
- **Why it works**: First contact with language sound; the classic Polish
  rhyme „*Co mówi kura…?"*[^17] is a near-universal early-childhood text.

### F. Liczymy do dziesięciu (Counting 1–10)

- **EN / PL**: Verbal counting / **„Licz ze mną"**
- **Goal**: stable-order counting, one-to-one correspondence[^18][^19].
- **Loop**: Agent: "Liczymy króliki! Jeden…" → kid: "Dwa!" → agent: "Trzy!" →
  alternating up to 10. Then "Ile było króliki?" — kid says "Dziesięć!".
- **Screen**: counter that grows: `1 . . . . .` → `1 2 . . . .` → up to 10.
- **Why it works**: NAEYC[^19] flags verbal alternating count as the easiest
  way to build the stable-order principle; matches Polish *podstawa*
  "wymienianie liczebników w prawidłowej kolejności".

### G. Ile jest? (One-to-one correspondence)

- **EN / PL**: Counting objects in a story / **„Ile?"**
- **Goal**: cardinality.
- **Loop**: Agent: "W ogrodzie były **trzy** żaby. Skoczyły **dwie** więcej. Ile
  jest teraz?" → kid: "Pięć!" → agent confirms.
- **Screen**: ASCII frogs: `🐸 🐸 🐸 + 🐸 🐸 = ?`
- **Why it works**: Cardinality lags stable-order by ~6 months[^18]; embedded
  in narrative ("story-problem") it's more accessible than abstract sums.

### H. Jaki to kolor? (I-Spy colours)

- **EN / PL**: Colour I-Spy / **„Widzę coś, co jest…"**
- **Goal**: colour vocabulary; descriptive language[^20].
- **Loop**: Agent: "Widzę coś, co jest **żółte** i świeci na niebie. Co to?" →
  kid: "Słońce!" → agent: "Tak! A teraz coś niebieskiego…"
- **Screen**: a tiny coloured ASCII art of the answer once guessed.
- **Why it works**: I-Spy is the classic preschool oral game[^20]; colour
  variant fits illiterate users; works in Polish without modification.

### I. Co to za zwierzę? (20 Questions, animals)

- **EN / PL**: 20-Questions about an animal / **„Zgadnij, jakie to zwierzę"**
- **Goal**: yes/no questioning, inference, categorisation[^21][^22].
- **Loop**: Agent picks an animal. Kid asks yes/no questions ("Czy mieszka
  w wodzie? Czy ma futro? Czy szczeka?"). Agent answers tak/nie. Kid guesses.
- **Screen**: a question counter `? 1 / 10` and a hint cloud `?????`.
- **Why it works**: Builds inferential language; Dzieciaki z Potencjałem[^22]
  notes the easier direction (agent asks, kid answers yes/no) is right for
  3–4-yo; older kids can take the asking role.

### J. Co jest w worku? (Mystery bag)

- **EN / PL**: What's in the bag / **„Co jest w worku?"**
- **Goal**: descriptive language, yes/no questioning[^23].
- **Loop**: Agent: "Mam coś w worku. Jest miękkie i mruczy. Co to?" → kid:
  "Kot!". Open-ended descriptive variant.
- **Screen**: `[ ? ? ? ? ]` worek; reveal the answer's ASCII when guessed.
- **Why it works**: Similar mechanic to I, but skill is *receptive* description
  rather than *generative* questioning — easier entry point.

### K. Simon mówi (Simon Says, body parts)

- **EN / PL**: Simon Says / **„Simon mówi"** (in Polish often **„Stary niedźwiedź"**
  variants or directly *„Powtarzaj za mną"*)
- **Goal**: listening, executive function (inhibit non-"Simon mówi" commands),
  body-part vocabulary[^24][^25].
- **Loop**: Agent: "Simon mówi: dotknij nosa." (kid does it) → "Dotknij ucha."
  (TRAP — no "Simon mówi"). We can't *see* the kid, but we can ask: "Czy
  dotknąłeś?" to elicit honesty + meta-awareness; or skip the trap and just
  use it as a movement game.
- **Screen**: body-part target as ASCII (👃 NOS).
- **Why it works**: classic listening/EF activity[^24]; movement breaks up
  sitting time; Polish translates 1:1.

### L. Echo / Powtórz po mnie (Auditory memory)

- **EN / PL**: Echo game / **„Echo"**
- **Goal**: auditory short-term memory, articulation imitation[^26].
- **Loop**: Agent says 2 words, kid repeats. Then 3 words. Then 4. Stop when
  the kid breaks. Variant: nonsense-syllable sequences ("ba-da-ga, powtórz!").
- **Screen**: `▌ ECHO ▐  ★ ★ ★` (one star per level passed).
- **Why it works**: Working memory underpins comprehension; echo is the
  natural early-childhood learning mechanism[^26]; cumulative sequence is a
  classic *digit-span*-style task adapted for words.

### M. Co się zmieniło? (Story-recall)

- **EN / PL**: Story sequencing / **„Co było pierwsze?"**
- **Goal**: narrative comprehension, temporal vocabulary (*najpierw, potem, na
  końcu*)[^27].
- **Loop**: Agent tells a 3–4 step story. Then asks "Co było **pierwsze**?
  Co **potem**? Co **na końcu**?". Kid retells.
- **Screen**: 3 boxes `[1] [2] [3]` filled in as the kid recalls.
- **Why it works**: Dialogic-reading research[^28] shows open-ended Wh-questions
  scaffold comprehension; sequencing is foundational for retell and inference
  at 4–5 yo.

### N. Dokończ historię (Story improv)

- **EN / PL**: Story completion / **„A potem…?"**
- **Goal**: narrative production, vocabulary, imagination.
- **Loop**: Agent: "Był sobie smok. Nie umiał ziać ogniem, tylko… BĄBELKAMI! Pewnego
  dnia smok poszedł… co dalej?" → kid says next beat → agent expands and prompts
  again.
- **Screen**: optional `create_animation` on the smok at the start, then
  static.
- **Why it works**: Dialogic reading[^28] PEER pattern (Prompt-Evaluate-Expand-
  Repeat) maps to this loop directly.

### O. Przeciwieństwa (Opposites)

- **EN / PL**: Opposites / **„Powiedz odwrotnie"**
- **Goal**: opposites vocabulary, abstraction[^29].
- **Loop**: Agent: "Mówię **duży**, ty mówisz?" → kid: "Mały!". List: duży/mały,
  ciepły/zimny, wysoki/niski, mokry/suchy, dzień/noc, jasny/ciemny, szybki/wolny.
- **Screen**: `DUŻY ↔ ?`
- **Why it works**: Opposites are abstract enough for 4–5-yo, concrete enough
  for voice. Common preschool curriculum item.

### P. Gdzie jest miś? (Spatial prepositions)

- **EN / PL**: Spatial preposition game / **„Gdzie jest miś?"**
- **Goal**: spatial vocabulary (*na, pod, w, za, obok, przed*)[^29].
- **Loop**: Agent: "Wyobraź sobie pudełko. Miś jest **NA** pudełku. A teraz, gdzie
  jest miś, jeśli wlazł do środka?" → kid: "W pudełku!". Reverses: agent
  describes scene, kid says where the bear is.
- **Screen**: small ASCII scene that reflects the current preposition.
- **Why it works**: CDC milestones[^29] place "in / on / under" at 36 months
  and additional prepositions through age 5; voice-only with imagination works.

### Q. Słowo na literę K (Word generation)

- **EN / PL**: Initial-letter word generation / **„Słowa na K!"**
- **Goal**: phoneme isolation in *production* (harder than recognition),
  vocabulary, lexical access[^30][^31].
- **Loop**: Agent: "Daj mi słowo, które zaczyna się od **K**!" → kid: "Kot!" →
  agent: "Brawo! Jeszcze jedno?" → "Krowa!". Stop when the kid stalls or after
  3 wins.
- **Screen**: a running list of accepted words `KOT · KROWA · KIWI`.
- **Why it works**: Production version of activity A; harder; great fluency
  ladder; the agent should accept *all* genuine K-words and gently reject
  non-K-words ("ej, *banan* zaczyna się od B! Spróbuj jeszcze raz na K.").

### R. Karaoke / piosenka (Sing-along)

- **EN / PL**: Sing-along / **„Zaśpiewajmy"**
- **Goal**: cultural literacy, prosody, memory, fun. Pre-baked songs: *„Głowa,
  ramiona, kolana, pięty"*[^32], *„Stary Donald farmę miał"*, *„Wlazł kotek
  na płotek"*.
- **Loop**: Agent uses [śpiew] tag and sings a line, pauses, kid sings next
  line, agent confirms + does next line.
- **Screen**: emoji body parts for HRKP `👃 👂 👄`.
- **Why it works**: Singing is the most enjoyable phonological-awareness
  training that exists; ElevenLabs v3 supports `[śpiew]` directly.

### S. Naśladowanie głosów / *Whisper game* (volume play)

- **EN / PL**: Volume control / **„Powiedz szeptem / głośno"**
- **Goal**: prosodic awareness, self-regulation, fun.
- **Loop**: Agent: "Powiedz **kot** szeptem!" → kid whispers → agent: "Teraz
  GŁOŚNO jak smok!" → kid yells.
- **Screen**: a meter ASCII `▁▁▂▂▃▃▄▄▅▅▆▆▇▇█`.
- **Why it works**: Calms or energises the child on purpose; great transition
  activity between higher-load games.

### T. Kategorie / Co tu nie pasuje (Categorisation)

- **EN / PL**: Odd-one-out / **„Co tu nie pasuje?"**
- **Goal**: categorisation, semantic networks.
- **Loop**: Agent: "Posłuchaj: jabłko, banan, **samochód**, gruszka. Co tu nie
  pasuje?" → kid: "Samochód!" → agent: "Bo to nie owoc!".
- **Screen**: `🍎 🍌 🚗 🍐  ← ?`
- **Why it works**: Builds semantic categorisation; preschool *podstawa* lists
  "klasyfikuje przedmioty" as a core outcome.

---

## 3. Shortlist scoring

Scoring rubric (each 1–10, higher is better):

- **VF — Voice feasibility**: Trivial over voice (10) vs. needs precise input (1).
- **LT — Latency tolerance**: Comfortable with a 1.5 s gap (10) vs. needs
  millisecond reaction (1).
- **PL — Polish fit**: Native Polish content available + cultural fit (10) vs.
  needs invention (1).
- **TR — Tool reuse**: Pure-prompt / extends `game_control` (10) vs. needs a
  new tool with new browser state (1).
- **FH — Fun hook**: Kid will pick it back up (10) vs. feels like school (1).
- **AGE — Age range**: 10 if it spans 3–6 yo gracefully; lower if narrow.

| # | Activity                  | VF | LT | PL | TR | FH | AGE | Total | Verdict      |
|---|---------------------------|----|----|----|----|----|----|-------|--------------|
| A | Zgadnij głoskę            |  9 |  9 | 10 |  9 |  7 |  8 | **52** | **SHORTLIST**|
| B | Klap sylaby               |  9 |  9 | 10 |  9 |  8 |  9 | **54** | **SHORTLIST**|
| C | Co się rymuje?            |  9 |  9 | 10 |  9 |  8 |  9 | **54** | **SHORTLIST**|
| D | Kończ rymowankę           |  9 |  9 | 10 | 10 |  9 |  8 | **55** | candidate, folds into R |
| E | Co mówi zwierzę?          | 10 | 10 | 10 | 10 |  9 |  7 | **56** | **SHORTLIST**|
| F | Liczymy do 10             |  9 |  9 |  9 |  9 |  6 |  9 | **51** | candidate    |
| G | Ile jest?                 |  8 |  8 |  9 |  8 |  7 |  8 | **48** | cut          |
| H | I-Spy kolory              |  7 |  9 |  9 |  9 |  8 |  9 | **51** | candidate    |
| I | Co to za zwierzę? (20Q)   |  8 | 10 |  9 |  9 |  9 |  7 | **52** | **SHORTLIST**|
| J | Worek                     |  9 | 10 |  9 |  9 |  8 |  8 | **53** | candidate, folds into I |
| K | Simon mówi                |  6 |  8 |  9 |  8 |  8 |  9 | **48** | cut (can't verify motion) |
| L | Echo                      |  9 |  9 | 10 | 10 |  6 |  9 | **53** | candidate    |
| M | Co było pierwsze?         |  8 |  9 |  9 |  8 |  6 |  6 | **46** | cut (narrow age) |
| N | Dokończ historię          |  9 | 10 |  9 | 10 |  9 |  9 | **56** | **SHORTLIST**|
| O | Przeciwieństwa            |  9 |  9 |  9 | 10 |  6 |  8 | **51** | candidate    |
| P | Gdzie jest miś?           |  7 |  9 |  9 |  8 |  6 |  7 | **46** | cut          |
| Q | Słowa na K                |  8 |  8 |  9 |  9 |  8 |  7 | **49** | candidate    |
| R | Karaoke                   |  9 |  9 | 10 | 10 | 10 | 10 | **58** | **SHORTLIST**|
| S | Szept / głośno            |  9 | 10 |  9 | 10 |  8 |  8 | **54** | candidate, transition glue |
| T | Co tu nie pasuje?         |  9 |  9 |  9 |  9 |  7 |  8 | **51** | candidate    |

### 3.1 Picks (6)

We pick the 6 highest-utility, lowest-cost activities that together cover the
full *podstawa* skill stack (phonology, vocabulary, narrative, culture,
inference) without overlap:

1. **B. Klap sylaby** — phonology, syllables. Core *podstawa* skill.
2. **C. Co się rymuje?** — phonology, rhyme. Universal early skill.
3. **E. Co mówi zwierzę?** — vocabulary, low-floor, works at 3 yo.
4. **I. Co to za zwierzę? (20Q)** — inference, yes/no question scaffold, works
   best 5–6 yo, has a child-asks-questions version for the harder tier.
5. **N. Dokończ historię** — narrative production, dialogic reading, highest
   creativity ceiling.
6. **R. Karaoke piosenek** — culture, prosody, fun, free engagement hook.

### 3.2 Why the cuts

- **F (Counting), H (I-Spy), L (Echo), O (Opposites), Q (Słowa na K), S (Volume),
  T (Co nie pasuje?)** are all good and cheap (mostly pure-prompt). They should
  ship as **prompt-only patches** (no new tool surface) and the agent can
  initiate them ad-hoc. Document them in §6 as v2 micro-games.
- **G (Story counting)** — fold into **R**/**N** rather than a standalone.
- **K (Simon mówi)** — we cannot verify the kid did the action, breaking the
  game's core mechanic. Could ship as a non-trap *Powtarzaj za mną* movement
  game but it duplicates **R**.
- **M (Story recall)** — useful but narrow age range and overlaps with **N**.
- **P (Spatial prepositions)** — requires imagining a scene the agent describes;
  ASCII screen could help but it'd need a real new tool with state. Defer.
- **D (Cloze rhyme)** — folds into **R** (sing-along) or even better, the agent
  spontaneously uses it in any narrative — no separate activity needed.
- **J (Mystery bag)** — folds into the receptive half of **I (20Q)**.

---

## 4. Detailed designs (shortlist)

For each shortlisted activity:

- **Tool surface** — recommended tool changes.
- **Polish dialogue script** — 6–10 turn example.
- **Content list** (where applicable) — starter Polish word/song lists.
- **Failure modes & recovery** — what the agent does when the kid mumbles or
  goes off-script.
- **Difficulty progression** — easy/medium/hard variants.
- **Memory hook** — `remember`/`recall_memory` integration.

### 4.1 KLAP SYLABY — `syl_*` actions on `game_control`

#### Tool surface

**Recommended: extend `game_control` enum.** The interaction is short
(start/word/answer/stop), state lives on screen only, no per-game state machine
needed. Mirror the RPS pattern.

```
game_control({action})
  syl_start        — start the syllable game; agent picks first word
  syl_next         — pick next word (any difficulty)
  syl_easy         — pick next 1–2 syllable word
  syl_hard         — pick next 3+ syllable word
  stop             — already exists; ends game
```

The browser needs only to render `miniShow("▌ SYLABY ▐", "BA-NAN  👏👏")` with
the current word + clap dots. The *word and its sylable count* come from the
LLM — keep that in the prompt, not in the browser, so we don't ship a Polish
word-list to JS. Acceptable trade-off: agent sometimes invents a non-existent
word; for our purpose (sylabowanie practice) that's fine.

Browser pseudo-code (sketch only, do not change yet):

```js
// inside switch(a) in clientTools.game_control
case "syl_start":  return sylStart();
case "syl_next":   return sylNext("any");
case "syl_easy":   return sylNext("easy");
case "syl_hard":   return sylNext("hard");
```

The handler would `miniShow` an empty board and notify the agent which word
class to pick next. The current Polish word is then taken from the agent's
spoken turn — the screen just shows the *agent's chosen* word, which we get by
having the agent call e.g. `notifyAgent` style or simpler: just leave the
screen as a fixed visual cue ("👏 KLAŚNIJ I LICZ SYLABY 👏") and let the agent
say the word, so we don't need to pipe text in at all.

**Simpler still: do not add `syl_*` at all.** Pure-prompt is sufficient — the
agent says "Klaszcz ze mną: **BA-NAN**, **dwie** sylaby!" without any tool. The
only reason to extend `game_control` is to swap the screen to a Sylaby-themed
panel; if we don't care about that, we save the new enum.

**Pick**: add only **`syl_start` and `stop`** to give the screen a thematic
panel and a clear ON/OFF state; everything else is the agent talking.

#### Dialogue (Polish, 8 turns)

> **Agent**: Wiesz co? Pobawmy się w sylaby! [śmiech] Ja powiem słowo, a ty
> klaszczesz i mówisz ile sylab. *[tool: game_control { syl_start }]* — KOT.
> Ko-t! Ile sylab?
>
> **Kid**: Jedna!
>
> **Agent**: Dokładnie tak! Jedna! A teraz trudniejsze: BA-NAN.
>
> **Kid**: Eee… dwa!
>
> **Agent**: Brawo! BA — NAN. [chichot] A teraz coś dłuższego: KO-MA-RY!
>
> **Kid**: Trzy?
>
> **Agent**: Tak! KO-MA-RY, trzy sylaby! Świetnie ci idzie. Jeszcze jedno: PIES.
>
> **Kid**: Jedna!
>
> **Agent**: Tak jest! PIES. Może powiesz mi słowo, a ja policzę sylaby?
> *[hands the turn back to the kid → infinite loop fuel]*

#### Starter word list (Polish, by syllable count)

The agent prompt should anchor on these so we don't get oddities. Curated from
*Przedszkole Nr 26 Maczki*[^4] and Pani Monia[^3].

- **1 sylaba**: kot, pies, lew, dom, las, sok, ząb, miś, sen, lód.
- **2 sylaby**: ma-ma, ta-ta, ba-nan, lo-dy, ka-wa, ko-tek, kro-wa, ku-ra,
  jab-łko, sło-ńce, ry-by, my-dło, mle-ko, dro-ga, ksią-żka.
- **3 sylaby**: ka-na-pa, sa-mo-chód, ka-pe-lusz, czer-wo-ny, ma-ka-ron, ko-cha-na,
  pa-ra-sol, ku-rie-rzy.
- **4 sylaby**: po-mi-do-ry, dy-na-mi-ka, ka-ru-ze-la, ele-fan-cik,
  cza-ro-dziej-ka.

Avoid in *easy tier*: words with /r/, /sz/, /cz/, /ż/, /ć/, /ś/ if the kid is
3–4. Reintroduce in *hard tier*.

#### Failure modes

- **Kid silent for >4 s** → agent: "Ej, jesteś tam? Powiedzmy razem: BA — NAN.
  Klaszcz: 👏 raz, 👏 dwa. Dwie sylaby!" — model gives the answer + invites
  imitation. No shame frame.
- **Kid says nonsense** ("dziewięć!") → agent: "Hehe, nie aż tyle! Posłuchaj
  jeszcze raz: BA — NAN. To dwie sylaby. Spróbujmy łatwiejsze: KOT."
- **Kid mispronounces** (kid: "babnan!") → agent ignores articulation, accepts
  the syllable count.
- **Kid goes off-topic** ("opowiedz o smokach") → agent: "OK! Po sylabkach
  wymyślę ci smoka — jedno słowo jeszcze: SMO-CZEK. Ile sylab?".

#### Difficulty progression

| Tier   | Word length        | Phoneme classes               |
|--------|--------------------|-------------------------------|
| Easy   | 1–2 syllables      | open syllables, no r/sz/cz/ż |
| Medium | 2–3 syllables      | any consonant                 |
| Hard   | 3–4 syllables      | clusters (str-, drz-)         |

Trigger advancement after 3 consecutive correct answers, regress after 2 wrong.

#### Memory hook

After a session, call `remember({text: "Sylaby: 4 lata, lubi proste słowa,
zna 1-2 sylaby"})`. Next session, `recall_memory({query: "sylaby"})` to skip
the warm-up and start at the right tier.

---

### 4.2 CO SIĘ RYMUJE? — `rym_*` actions on `game_control`

#### Tool surface

**Recommended: extend `game_control` enum** with `rym_start`, `rym_yes`,
`rym_no`, `stop`. The screen needs to show the target word and two/three
options — that's a clear state worth a tool, mirroring RPS's three buttons.

```
game_control({action})
  rym_start    — show "▌ CO SIĘ RYMUJE? ▐" panel, agent picks word + options
  rym_yes      — kid said option A — agent decides if right
  rym_no       — kid said option B
  (no need for "rym_third" — keep it 2-option binary like RPS)
```

Browser stub:

```js
case "rym_start":  return rymStart();  // miniShow("▌ CO SIĘ RYMUJE? ▐", " KOT\n A) LOT\n B) MAMA")
case "rym_yes":    return rymPick(true);
case "rym_no":     return rymPick(false);
```

The agent picks the actual word and rhyme distractors per turn (in Polish, in
the prompt). The browser just renders whatever the agent's last `notifyAgent`
hint set. Actually — simpler still: the agent says it out loud, the browser
panel is *thematic only* (`miniShow("▌ CO SIĘ RYMUJE? ▐", "★")`). Same trick as
syllables: don't pipe text, just paint mood.

**Pick**: ship `rym_start` + `stop` only. Agent does the rest.

#### Dialogue (Polish, 7 turns)

> **Agent**: A teraz rymy! [chichot] Powiem słowo, a ty zgadnij które się z
> nim rymuje. *[game_control { rym_start }]* Słowo: **KOT**. Co się rymuje:
> **LOT** czy **MAMA**?
>
> **Kid**: Lot!
>
> **Agent**: Brawo! Bo *kot–lot*, słychać KOT-LOT, dwa OT-y! [śmiech] Następne:
> **MAMA**. **TATA** czy **DAMA**?
>
> **Kid**: Tata?
>
> **Agent**: Hmm, prawie! Ale słuchaj — mama-DAMA. Mama-tata. Mama-DA-MA się
> rymuje! Słyszysz to AMA? Spróbujmy jeszcze raz: **DOM**. **KOT** czy **GROM**?
>
> **Kid**: Grom!
>
> **Agent**: Tak! DOM-GROM, brzmią tak samo na końcu! Jesteś rymarzem! [śmiech]

#### Starter rhyme pairs (Polish, ~30 entries)

Single-syllable seed (easy):

- KOT — LOT, PŁOT, MOT, TOR (no — different vowel)
- DOM — GROM, ROM, TOM
- LIS — RYS, NIS, BIS
- MUR — SZNUR, TYR
- RAK — MAK, ZNAK, ŁAK
- SOK — KROK, ROK
- KOŚĆ — GOŚĆ, MOŚĆ

Two-syllable seed:

- MAMA — DAMA, RAMA, TAMA, PIŻAMA
- KOTEK — PŁOTEK, MŁOTEK, ZŁOTEK
- LALA — SKALA, FALA, HALA
- WODA — MODA, NIEPOGODA
- NOGA — DROGA, ROGA, TOGA
- LATO — BŁOTO (no — vowel), ZŁOTO
- CHATA — TATA, ARMATA

Phrase rhymes (cloze, harder, for the rhyming-cloze fold-in from D):

- "Idzie pani stuk, stuk, dziadek z laską ___" → **puk-puk**[^15]
- "A-a-a, kotki ___" → **dwa**[^14]
- "Wlazł kotek na ___" → **płotek**
- "Hop hop, mała ___" → **żabka**

#### Failure modes

- **Kid picks wrong** → recast: "Hmm, posłuchaj jeszcze raz: ma-ma, da-ma.
  Słyszysz AMA na końcu? Tak. A kotek-DAMA? Nie pasuje, prawda? Spróbuj jeszcze
  raz!"
- **Kid says a word that isn't either option** → if that word *also* rhymes,
  praise it: "O! TAMA też się rymuje z MAMA, brawo!". If not, redirect: "Hehe,
  ale mówiłem dwa do wyboru: TATA albo DAMA?".
- **Kid silent** → agent gives the answer with stress + invites repeat.

#### Difficulty progression

- **Easy**: 1-syllable target, 1 obvious distractor.
- **Medium**: 2-syllable, more subtle distractor (different vowel).
- **Hard**: open-ended ("Daj mi cokolwiek, co rymuje się z LATO!").

#### Memory hook

`remember({text: "Rymy: zna proste 1-syl, gubi się przy 2-syl"})` after 3
sessions. Use to skip warm-up.

---

### 4.3 CO MÓWI ZWIERZĘ? — pure prompt, no new tool

#### Tool surface

**Recommended: pure prompt + reuse existing `show_animation`.** The interaction
is a Q&A. No state worth screening. When the kid names an animal we have art
for (`cat`, `dog`, `fish`, `butterfly`, `dino`), use `show_animation` to reward
them. No new `game_control` actions.

If we want a "ZWIERZĘTA" themed panel during the game, the cheapest path is to
also add `zwierzeta_start` + `stop` to `game_control` purely as a mood-setter.
But this is optional and probably unnecessary — the existing animations are
already the reward layer.

**Pick**: zero new tool surface; only a prompt patch.

#### Dialogue (Polish, 6 turns)

> **Agent**: Hej, znasz zwierzęta? Sprawdzimy! Co mówi krowa?
>
> **Kid**: Muu!
>
> **Agent**: Mistrz! [śmiech] A co mówi piesek?
>
> **Kid**: Hau-hau!
>
> **Agent**: Tak jest! *[tool: show_animation { dog }]* Patrz, piesek się
> macha ogonem! A teraz trudniejsze: co mówi koza?
>
> **Kid**: Mee?
>
> **Agent**: Brawo! Me-me-me! A teraz odwrotnie: zgadnij, co to za zwierzę:
> Ko-ko-ko!
>
> **Kid**: Kura!
>
> **Agent**: TAK! Kura znosi jajka i robi ko-ko-ko! [chichot] Jesteś jak
> zoologiem!

#### Content (Polish animal sounds, full canonical list[^17])

| Zwierzę | Polski dźwięk |
|---------|---------------|
| krowa   | muu / mu-mu   |
| pies    | hau-hau       |
| kot     | miau          |
| kura    | ko-ko-ko      |
| kogut   | kukuryku      |
| kaczka  | kwa-kwa       |
| świnia  | chrum-chrum   |
| koza / owca | me-me-me  |
| baran   | be-be-be      |
| żaba    | kum-kum       |
| bocian  | kle-kle       |
| wrona   | kra-kra       |
| wąż     | sssss         |
| lew     | rrraaa / rryyk |
| sowa    | hu-hu         |
| pszczoła | bzzz         |
| osa     | bzzz (wyższe) |
| mucha   | bzzz          |
| koń     | ihaaa         |
| ryba    | — *(„nic, ryby nie mają głosu"[^17])* — great joke beat |

Animations available for reward: `cat, dog, fish, butterfly, dino` and an
`create_animation({subject})` for anything missing ("krowa", "kura").

#### Failure modes

- **Kid imitates a different sound** (kid: krowa "mlee") → accept loosely: "Mu,
  tak! Krowa robi muu! [śmiech]"
- **Kid says English** ("moo") → recast: "Po polsku mówimy MUU! Tak samo
  prawie!"
- **Kid stays silent** → agent fills: "Krowa robi MUUU. Powiedz ze mną: MUU!".

#### Difficulty progression

- **3–4 yo (Easy)**: krowa, pies, kot, kura, kaczka, koza.
- **5 yo (Medium)**: kogut (kukuryku — multi-syllable!), bocian (kle), wąż (s),
  reverse direction (agent imitates → kid names).
- **6 yo (Hard)**: insects (pszczoła/osa/mucha — all bzz, kid has to
  differentiate by description), foreign animal (słoń — *trąbi*, lew — *ryczy*),
  trick question with ryba.

#### Memory hook

`remember({text: "Ulubione zwierzę: kura — robi ko-ko-ko"})` whenever the kid
spontaneously names an animal as their favourite. Use in story prompts.

---

### 4.4 CO TO ZA ZWIERZĘ? — 20-questions, pure prompt + score panel

#### Tool surface

**Recommended: extend `game_control` with `quiz_start` + `quiz_hint` + `stop`**,
where the screen renders a tiny question counter `? 3/10` and a hint window for
the description as it accumulates. State (the secret animal, used hints,
remaining hints) lives in the LLM context, not the browser — the browser is
just a scorekeeper.

```
game_control({action})
  quiz_start    — start a 20Q-style game with the screen panel
  quiz_hint     — increment the hint counter (browser bumps the "?N/10")
  stop          — end
```

This needs slightly more state than RPS/TTT to be useful. Alternative: pure
prompt, no tool, agent counts in its head. That works but loses the visual
"hint counter" feedback. Pick whichever feels less risky.

**Pick**: `quiz_start` + `quiz_hint` + reuse `stop`. ~25 lines of JS in
`miniShow` style.

#### Dialogue (Polish, 8 turns)

> **Agent**: Mam zagadkę! Wymyśliłem zwierzę. Zgadnij jakie! Możesz pytać i ja
> powiem TAK albo NIE. Albo opisuję krok po kroku, jak wolisz!
> *[game_control { quiz_start }]*
>
> **Kid**: Opisuj!
>
> **Agent**: Dobra! *[quiz_hint]* Pierwsza wskazówka: to zwierzę ma cztery łapy.
>
> **Kid**: Pies?
>
> **Agent**: Hmm, nie powiem! [chichot] Następna *[quiz_hint]*: jest duże,
> większe od ciebie. I ma trąbę!
>
> **Kid**: SŁOŃ!
>
> **Agent**: TAAAK! Słoń! [śmiech] Świetnie zgadłeś! *[show_animation { dance }
> as celebration]* Jeszcze jedno?
>
> **Kid**: Tak!
>
> **Agent**: OK! *[quiz_start]* Wskazówka *[quiz_hint]*: jest małe i lata. Nie
> jest ptakiem. Ma kropki.
>
> **Kid**: Biedronka!
>
> **Agent**: BRAWO! Biedronka! Czerwona z czarnymi kropkami!

Reverse version (harder, 5–6 yo): agent picks an animal, kid asks tak/nie
questions. The PT-22[^22] research confirms 3–4-yo do better as *answerers*, 5–6
as *askers*.

#### Animal seed list (with one-liner hint chain)

For the agent prompt:

- **Słoń**: duży, trąba, szare, Afryka.
- **Żyrafa**: bardzo wysoka, długa szyja, w cętki.
- **Lew**: drapieżnik, grzywa, ryczy, król.
- **Małpa**: w dżungli, lubi banany, wisi na drzewach.
- **Żółw**: powolny, ma skorupę, długo żyje.
- **Kret**: pod ziemią, kopie tunele, czarne futerko.
- **Biedronka**: mała, lata, czerwona w kropki.
- **Bocian**: duży ptak, czerwone nogi, je żaby.
- **Wąż**: bez nóg, syczy, jest długi.
- **Krokodyl**: w wodzie, duże zęby, zielony.
- **Pies**: cztery łapy, szczeka, lubi spacery, domowy.
- **Kot**: cztery łapy, mruczy, łapie myszy.
- **Konik morski**: w morzu, mały, ogonek się zwija.

#### Failure modes

- **Kid keeps guessing wrong** → agent gives a much narrower hint after 3
  wrong: "Pomogę ci! To zwierzę robi: MUU." (animal-sound reveal as final
  hint) or "Zaczyna się na K i ma dwie sylaby."
- **Kid says something weird** ("dinozaur" when answer is "kot") → accept
  with grace: "[śmiech] Świetny pomysł! Ale to nie dinozaur. Daję wskazówkę:
  zwierzę domowe i mruczy."
- **Kid wants to switch role** (asks to be the one who picks) → agent: "Super!
  Pomyśl o zwierzęciu i nie mów mi co to. Ja będę pytać. Czy mieszka w
  domu?". Switch quiz_start mode in prompt.

#### Difficulty progression

- **Easy (3–4)**: agent describes, kid names. Common animals.
- **Medium (5)**: agent describes more abstractly. Add wild animals.
- **Hard (6)**: kid picks, agent guesses. Kid has to formulate questions ("czy
  jest duże?"). Or: agent gives only 3 hints max before reveal.

#### Memory hook

`remember({text: "Słoń to ulubione zwierzę"})` if a particular animal recurs.
Recall on next session: "Pamiętam, że lubisz słonie! Zagrajmy znowu w
zagadki?".

---

### 4.5 DOKOŃCZ HISTORIĘ — pure prompt + `create_animation` for the hero

#### Tool surface

**Recommended: pure prompt, reuse `create_animation` and `show_animation`** for
the hero/scene. Story production is text in / text out; no game state belongs
in the browser. Optional `story_start` action on `game_control` to dim the
screen to a "STORY MODE" panel `▌ HISTORIA ▐ … 📖` as a mood-setter.

**Pick**: pure prompt. Maybe `story_start` + `stop` purely to switch the
screen to a quiet "story listening" panel; otherwise reuse the dance/rocket
animations as scene illustrations.

#### Dialogue (Polish, 9 turns)

> **Agent**: Posłuchaj! Wymyślimy historię razem. Był sobie smok, ale ten smok
> nie ział ogniem — ział… **bąbelkami**! [śmiech]
> *[tool: create_animation { subject: "smok z bąbelkami" }]* Smok mieszkał w
> jaskini. Pewnego dnia smok wstał i poszedł na śniadanie. Co zjadł na
> śniadanie?
>
> **Kid**: Naleśniki!
>
> **Agent**: Bombowo! Naleśniki z dżemem! [chichot] Smok zjadł 10 naleśników i
> beknął — POW! Wyleciał wielki bąbel. A potem smok się rozejrzał i zobaczył…
> kogo?
>
> **Kid**: Małą myszkę!
>
> **Agent**: O! Mała myszka, taka malutka. I co zrobił smok? Zjadł myszkę?
>
> **Kid**: Nie! Zaprzyjaźnił się!
>
> **Agent**: Super! Smok i myszka zaprzyjaźnili się. Razem polecieli na… gdzie?
> Daleko czy blisko?
>
> **Kid**: Na księżyc!
>
> **Agent**: Na KSIĘŻYC! *[show_animation { rocket }]* I co tam zobaczyli na
> księżycu?
>
> **Kid**: Sera!
>
> **Agent**: SERA! [śmiech] Mnóstwo sera, bo księżyc jest z sera! Smok bąbluje
> ser, myszka go zjada. I żyli długo i bąblowali. Koniec! Brawo, opowiedziałeś
> świetną historię. Chcesz jeszcze jedną?

#### Dialogic-reading scaffolding[^28] (PEER pattern)

Each agent turn follows **Prompt → Evaluate → Expand → Repeat**:

- **Prompt**: open Wh-question ("Co zjadł na śniadanie?").
- **Evaluate**: name the response ("Naleśniki!").
- **Expand**: add detail ("Naleśniki z dżemem, 10 sztuk").
- **Repeat**: hand back ("A co zrobił potem?").

#### Seed-story bank (so the agent doesn't have to invent from scratch)

Five reusable openers in the prompt:

1. *Smok co ział bąbelkami* — protagonist tries an everyday thing.
2. *Kot pirat na morzu* — a quest narrative.
3. *Dinozaur w szkole* — fish-out-of-water comedy.
4. *Magiczna marchewka w ogrodzie* — folktale-shaped.
5. *Ufoludek w piaskownicy* — encounter story.

Each opener has 3–4 beats with kid-facing question prompts pre-baked.

#### Failure modes

- **Kid silent or "nie wiem"** → agent gives 2 multiple-choice options: "Co
  zjadł — naleśniki czy parówki?" — Hobson's choice.
- **Kid says something rude/weird** → reframe upward: kid: "smok zrobił kupę"
  → agent: "[śmiech] No tak, smoki też muszą kupę! A potem smok zobaczył co?".
- **Kid wanders** ("a tata wrócił do domu?") → agent integrates: "Tak! Tata
  wrócił i przywiózł smokowi nowe bąbelki. A smok co zrobił?".

#### Difficulty progression

- **Easy (3–4)**: 3-beat story, agent does 80% of the narration, kid fills
  single-word slots ("co zjadł?").
- **Medium (5)**: 5-beat story, kid picks plot turns.
- **Hard (6)**: open-ended; agent only prompts, kid carries narrative; agent
  asks "co było na końcu?" to elicit retell.

#### Memory hook

`remember({text: "Bohater: smok-bąblarz, postać wspólnie wymyślona"})`. Next
session recall: "Pamiętasz naszego smoka-bąblarza? Co dziś robi?". Massive
engagement win — recurring character is preschool catnip.

---

### 4.6 KARAOKE / PIOSENKA — pure prompt + `play_on_speaker` fallback

#### Tool surface

**Recommended: pure prompt** using ElevenLabs v3 `[śpiew]` tag, with
`play_on_speaker({action: "music"})` as a fallback for songs the agent can't
sing well. Karaoke (call-and-response) is the *primary* mode; HomePod-played
song is the backup mode if the kid asks for "muzyka" with no specific song.

Add `sing_start` + `stop` to `game_control` purely for the on-screen lyric
panel — but only if we want a visible cue. Most simply: zero new tools.

**Pick**: pure prompt. The agent sings, pauses, listens, sings the next line.
The kid sings the response line. If the kid wants real recorded music
("posłuchać"), the agent calls `play_on_speaker`.

#### Dialogue (Polish, 8 turns)

> **Agent**: Zaśpiewamy! [śpiew] Głowa, ramiona, kolana, pięty… kolana, pięty,
> kolana, pięty! [/śpiew] Teraz ty! Co dalej?
>
> **Kid**: Głowa, ramiona, kolana, pięty… oczy, uszy, usta, nos!
>
> **Agent**: [śmiech] BRAWO! Pokaż mi nosa! Dotknij nosa!
>
> **Kid**: *(touches nose)* O tu!
>
> **Agent**: Świetnie! A teraz szybciej! [śpiew szybciej] Głowa, ramiona,
> kolana, pięty, oczy, uszy, usta, nos! [/śpiew] Jeszcze raz?
>
> **Kid**: TAAK!
>
> **Agent**: A teraz jakąś inną! Znasz „Wlazł kotek na płotek"? [śpiew] Wlazł
> kotek na płotek i mruga… [pauza]
>
> **Kid**: …ładna to piosenka niedługa!
>
> **Agent**: O! Pamiętasz! Brawo!

#### Song repertoire (Polish preschool canon)

Curated from Przedszkole Nr 13[^32] and Mamy-Mamom[^15]:

1. **Głowa, ramiona, kolana, pięty** — call-and-response body parts, classic
   action song[^32].
2. **Wlazł kotek na płotek** — folk lullaby.
3. **Stary Donald farmę miał** (PL of *Old MacDonald*) — animal-sound
   reinforcement, ties into 4.3.
4. **A-a-a, kotki dwa** — lullaby, slow tier.
5. **Kółko graniaste** — round-dance.
6. **Pingwinek** — movement song.
7. **Sroczka** — finger-tapping rhyme[^15].
8. **Idzie pani stuk-stuk** — sound-effect rhyme[^15].
9. **Jadą jadą misie** — travel song.
10. **Jeżeli ci wesoło** (PL of *If You're Happy and You Know It*).

#### Failure modes

- **Kid doesn't know the song** → agent: "OK, ja zaśpiewam, ty słuchaj i
  powtarzaj!". Pure echo mode.
- **Kid sings nonsense** → agent: "[śmiech] Świetna własna wersja! Posłuchaj
  oryginału: …".
- **Kid wants a song we don't have memorised** → `play_on_speaker({action:
  music, query: "<song name>"})` on HomePod.

#### Difficulty progression

- **Easy (3–4)**: agent sings whole song, kid joins on chorus.
- **Medium (5)**: call-and-response, agent pauses on last word per line.
- **Hard (6)**: kid sings solo verse, agent does backing vocals; kid teaches
  agent a new song they know from przedszkole.

#### Memory hook

`remember({text: "Ulubiona piosenka: 'Głowa ramiona' — śpiewa wszystkie zwrotki"})`.
Use to greet the kid: "Cześć! Chcesz zaśpiewać naszą Głowę-ramiona?".

---

## 5. New tools needed (concrete stubs)

We deliberately minimise new tool surface. The shortlist needs only the
following additions, all expressed as enum extensions to the **existing**
`game_control` tool. No new top-level tools.

### 5.1 `game_control` enum extension

Adds 9 new actions (3 new sub-games, each with a `_start` and helpers). Mirrors
the existing `rps_*` / `ttt_*` shape exactly.

```
game_control({action})
  // existing
  snake_start | left | right | up | down | stop
  rps_start | rps_rock | rps_paper | rps_scissors
  ttt_start | ttt_1 .. ttt_9
  // NEW (this brief)
  syl_start                     — show "▌ SYLABY ▐" panel
  rym_start                     — show "▌ RYMY ▐" panel
  quiz_start                    — show "▌ ZAGADKA ▐" panel; reset hint counter
  quiz_hint                     — bump hint counter on the quiz panel
  // OPTIONAL mood-setters (cosmetic only; could skip)
  story_start                   — switch screen to "▌ HISTORIA 📖 ▐" panel
  sing_start                    — switch screen to "▌ ŚPIEWAJMY 🎵 ▐" panel
```

Minimum-viable additions: **`syl_start, rym_start, quiz_start, quiz_hint`** (4).
The two optional mood-setters can wait until we want polish.

### 5.2 Browser handler stubs (mirror `rpsStart` / `tttStart`)

Sketch — DO NOT IMPLEMENT YET, this is spec only:

```js
// after rpsStart() in index.html
function sylStart() {
  miniShow("▌ SYLABY ▐",
    "👏 KLAŚNIJ I LICZ 👏\n\n  RAZ - DWA - TRZY");
  setFace("speaking");
  miniEndSoon(60_000);            // syllable bouts run up to 1 min
  return "Gramy w sylaby!";
}

function rymStart() {
  miniShow("▌ CO SIĘ RYMUJE? ▐",
    "★ Słuchaj uważnie ★\n\n A) ...\n B) ...");
  setFace("speaking");
  miniEndSoon(60_000);
  return "Gramy w rymy!";
}

const QUIZ = { hints: 0 };
function quizStart() {
  QUIZ.hints = 0;
  quizDraw("Wymyśliłem zwierzę!");
  setFace("thinking");
  return "Mam zagadkę!";
}
function quizHint() {
  QUIZ.hints += 1;
  quizDraw("Wskazówka " + QUIZ.hints + "/5…");
  return "OK!";
}
function quizDraw(line) {
  miniShow("▌ ZAGADKA ▐",
    "🦁 ❓ 🐘 ❓ 🦒\n\n" + line + "\n\nPytaj albo zgaduj!");
}

// in clientTools.game_control switch:
case "syl_start":   return sylStart();
case "rym_start":   return rymStart();
case "quiz_start":  return quizStart();
case "quiz_hint":   return quizHint();
```

No new JSON schemas. The existing `game_control({action: string})` already
accepts free-form action strings; this is purely an enum extension on the
prompt side. **The actual JSON schema for `game_control` in `index.html`
already takes a plain string `action` — no schema change required, only an
enum-docs comment.**

### 5.3 No new top-level tools

Every shortlisted activity is reachable through:

- `game_control` (extended) for state-bearing games.
- `show_animation` / `create_animation` for visual rewards.
- `play_on_speaker` for HomePod fallback music.
- `remember` / `recall_memory` for cross-session progress.
- Pure prompt for everything else.

---

## 6. Agent prompt patches

Lines to append to the `═══ CO POTRAFISZ — UŻYWAJ TYCH NARZĘDZI! ═══` block in
`project/seed_polish_agents.py` (around L97–108). Mirror the existing tone
("WĄŻ: snake_start, potem left/right…"). Polish, terse, instruction-flavoured.

### 6.1 Shortlist patches (must-ship)

```text
2a. GRAĆ W SYLABY — narzędzie game_control. SYLABY: syl_start, potem mów krótkie
słowo (kot, banan, makaron), dziel je głośno na sylaby (BA-NAN), pytaj "ile
sylab?". Dla 3-4 lat: 1-2 sylaby (kot, mama, banan). Dla 5-6 lat: 3-4 sylaby
(samochód, makaron, karuzela). Pochwal niezależnie od odpowiedzi.

2b. GRAĆ W RYMY — narzędzie game_control. RYMY: rym_start, potem powiedz słowo
i daj dwa do wyboru (KOT — LOT czy MAMA?). Akceptuj prawie-rymy. Po 3
poprawnych zaproponuj otwarte ("Daj mi coś co rymuje się z LATO!").

2c. GRAĆ W ZAGADKI O ZWIERZĘTACH — narzędzie game_control. ZAGADKI:
quiz_start, potem opisuj zwierzę po kawałku (cztery łapy → duże → ma trąbę →
słoń!). Każda wskazówka wywołuje quiz_hint. Po 3-4 wskazówkach pomóż mocniej
(pierwsza głoska albo dźwięk zwierzęcia). Czasem odwracaj: dziecko wymyśla,
ty pytasz tak/nie.

2d. ŚPIEWAĆ KARAOKE — sing without a tool, używaj [śpiew]. Klasyki: "Głowa,
ramiona, kolana, pięty", "Wlazł kotek na płotek", "Stary Donald farmę miał",
"A-a-a, kotki dwa". Śpiewaj linię, pauza, dziecko śpiewa kolejną. Jeśli nie
zna — śpiewasz całą, ono powtarza.

2e. OPOWIADAĆ WSPÓLNĄ HISTORIĘ — bez narzędzia, czasem create_animation na
bohatera. Otwórz dziwnym pomysłem ("smok co ział bąbelkami"). Pytaj otwarte
Wh-pytania ("Co potem? Kogo spotkał? Gdzie poszli?"). Po każdej odpowiedzi
rozwiń i pytaj dalej. 4-6 beats, koniec puentą.

2f. PYTAĆ "CO MÓWI ZWIERZĘ?" — bez narzędzia, czasem show_animation jako
nagroda. "Co mówi krowa?" → muu. "Co mówi kura?" → ko-ko-ko. Po 4-5
zwierzętach odwróć: "Ko-ko-ko — kto tak robi?". Akceptuj angielski (moo →
"po polsku MUU!").
```

### 6.2 V2 prompt-only micro-games (no new tool, ship as one-liners)

Append after 2f:

```text
3. INNE MAŁE ZABAWY GŁOSOWE (bez narzędzi, sam proponuj):
- LICZENIE NA ZMIANĘ do 10 ("jeden!" — "dwa!" — "trzy!" — kto pierwszy zgubi).
- I-SPY KOLORY ("Widzę coś żółtego co świeci na niebie — co to?").
- ECHO ("powtórz po mnie: ba-da-ga") — dodawaj sylaby aż zgubi.
- PRZECIWIEŃSTWA ("duży — ?" → mały).
- CO TU NIE PASUJE ("jabłko, banan, samochód, gruszka — co tu nie pasuje?").
- SZEPT/GŁOŚNO ("powiedz KOT szeptem!" → "teraz GŁOŚNO jak smok!").
```

### 6.3 Trigger guidance

After the master list, append:

```text
═══ KIEDY ZAPROPONOWAĆ NAUKĘ-ZABAWĘ ═══
Gdy dziecko marudzi, nudzi się, albo skończyło inną grę — nie czekaj, sam
zaproponuj jedną z zabaw powyżej. Rotuj między fonologią (sylaby, rymy),
słownictwem (zwierzęta, kolory) i historią (opowieść, piosenka). Nie więcej
niż 5 minut tej samej zabawy. Pochwała > poprawka. Jak nie zgadnie — daj
podpowiedź albo wzorzec, nigdy "źle".
```

---

## 7. Safety, fairness, dyslexia-friendliness

### 7.1 Pronunciation tolerance

Polish phoneme acquisition timeline (rough — Polish speech therapy norms[^4]):

| Wiek  | Powinno być nabyte                                |
|-------|---------------------------------------------------|
| 3 yo  | p, b, t, d, k, g, m, n, f, w, l, j, samogłoski  |
| 4 yo  | s, z, c, dz                                       |
| 5 yo  | sz, ż, cz, dż                                     |
| 5–6yo | r (różnicowanie l/r)                             |
| 6yo   | wszystkie głoski w mowie codziennej               |

Practical rules baked into the prompt:

- **Don't grade pronunciation.** ASR may transcribe "lyba" as "ryba"
  anyway. If meaning is clear, accept.
- **r → l, sz → s, cz → c substitutions: silently accept.** Don't correct
  unless the kid asks "jak to się mówi?".
- **Easy-tier word lists must avoid r, sz, cz, ż, ć, ś, dz, dż.** Re-introduce
  in the hard tier (5–6 yo).
- **For the 3-yo, accept English approximations** ("moo", "woof") and reflect
  back Polish: "Tak! Po polsku MUU!".

### 7.2 Let the kid win

- RPS: already balanced (random); over a long enough session, kid wins ~33%.
  *No change needed*.
- TTT: agent's `tttAi()` is already a perfect minimax — for 5–6-yo this is too
  hard. **Recommend** weakening: pick a random valid move 50% of the time when
  the agent has no immediate winning/blocking move. Already implemented for
  win/block; just downgrade the centre/corner preference into a coin-flip.
  (This is an existing-code suggestion; out of scope for *this* brief.)
- Sylaby / Rymy / Zagadki: agent always confirms the correct answer eventually;
  hint counter on quiz prevents endless stuckness; sylaby/rymy don't have a
  win/lose state.

### 7.3 No shame on errors

The prompt-patch rule "pochwała > poprawka" matters. Concrete language to
encode:

- **Never** say "źle", "nie", "tak nie jest". Use "hmm", "prawie", "posłuchaj
  jeszcze raz".
- **Reformulate**, don't correct: kid says "kuń" → agent says **"Koń! Tak, koń
  robi i-haa!"** with stress on the right form. The correct version IS the
  feedback.
- **Self-deprecating retry**: "Ojej, pomyliłem słowo! Spróbujmy znowu". Lets
  the kid try again without it being "their" mistake.
- **Switch-to-easier**: after 2 wrongs in a row, drop tier. Code in the prompt:
  "po 2 błędach z rzędu, weź łatwiejsze słowo, nie ten sam poziom".

### 7.4 Five-minute cap

The existing prompt already enforces "po 5 min zaproponuj inną postać". Apply
the same to *within-character* activities: after ~5 minutes of one game (sylaby
loop, story arc, song marathon), proactively suggest a switch *of activity*.
This is mostly a prompt nudge:

```text
═══ ROTACJA ZABAW ═══ Po 5 minutach tej samej zabawy, zaproponuj inną
(animacja → gra → piosenka → historia → wołanie innej postaci). Dzieci szybko
się męczą jednym schematem.
```

### 7.5 Privacy / data hygiene

The existing `remember` writes a durable note to the per-character memory
store. For learning-game progress:

- Store **only the tier** ("sylaby tier 2"), **never** the kid's wrong
  answers verbatim. They aren't useful and they're embarrassing for the kid
  later.
- Memory keys per activity: `syl_tier`, `rym_tier`, `quiz_pref` (e.g. "lubi
  duże zwierzęta"), `story_hero` (e.g. "smok-bąblarz"), `song_fav`.

### 7.6 Accessibility for non-typical speakers

Specific notes for kids with speech delays / suspected dyslexia[^11][^12]:

- Sylaby and Rymy are *exactly* the right games — they're the gold-standard
  phonological-awareness training for dyslexia risk.
- Quiz and Story are great for vocabulary even when articulation is delayed.
- Karaoke / song repetition is strong: prosody scaffolds articulation.
- Avoid the production-direction phoneme game ("daj mi słowo na K") for
  speech-delayed kids — too hard. Keep recognition-direction ("na jaką
  głoskę zaczyna się kot?") only.

---

## 8. Open questions

1. **Custom secret word per kid for syllable / rhyme games**: do we want to
   pre-seed words a particular kid struggles with from their `remember`
   history? Would need a small per-child word-bank scheme. *Recommendation:
   defer to v2.*
2. **Real-time scoring**: should we expose a streak counter ("3 z rzędu!")?
   Adds engagement but encourages competition / shame on miss. *Recommendation:
   no for v1, soft praise only.*
3. **Polish word-list canonicalisation**: who owns the curated word lists — the
   agent prompt (LLM picks) or a static JSON? Right now the prompt-owned route
   is simpler but risks the LLM inventing nonsense words. Worth testing both.
4. **TTT difficulty**: minimax is too strong for 5-yo; out of scope here but
   flagging — see §7.2.
5. **ASR confidence threshold**: if Whisper transcribes the kid's utterance
   with low confidence, should the agent ask "co?" or just guess? Right now
   guessing — could be wrong. Worth measuring with the existing telemetry.
6. **Sing-mode latency**: `[śpiew]` tag adds TTS latency in v3; the
   call-and-response loop may feel sluggish. Pilot on Steve / Pikachu first
   (low-stakes characters) before rolling to Sonic.
7. **Multi-kid sessions**: many of these games assume one kid. Do we want a
   "wasza kolej!" rotation for 2+ kids? Probably yes for sylaby and rhymes —
   future work.
8. **Group-vs-solo word selection**: if 2 kids of different ages are present,
   which tier should sylaby/rymy default to? *Recommendation: ask "ile masz
   lat?" at the start of the game-block, store in session.*
9. **Mute-button equivalent for shy kids**: a kid who refuses to talk has no
   on-ramp. Solution today is the `go_to_sleep` tool. Could add a "guess for
   me!" mode where agent plays both sides and only invites the kid to copy.
   *Recommendation: defer; covered by `[śpiew]` echo mode in 4.6.*
10. **What we genuinely couldn't research**: pediatric-norms WER on Polish
    preschool ASR specifically. Open research gap.

---

## 9. Sources

All URLs accessed 2026-06-01.

[^1]: Ośrodek Rozwoju Edukacji (ORE), *Wychowanie przedszkolne i edukacja
  wczesnoszkolna — Podstawa programowa z komentarzem*, MEN, 2017.
  <https://ore.edu.pl/wp-content/uploads/2017/05/wychowanie-przedszkolne-i-edukacja-wczesnoszkolna.-pp-z-komentarzem.pdf>

[^2]: ISAP — *Rozporządzenie Ministra Edukacji Narodowej z dnia 14 lutego 2017 r.
  w sprawie podstawy programowej wychowania przedszkolnego*.
  <https://isap.sejm.gov.pl/isap.nsf/download.xsp/WDU20170000356/O/D20170356.pdf>

[^3]: Pani Monia, *Jak uczyć dziecko czytać sylabami? Propozycje zabaw*, 2020.
  <https://panimonia.pl/2020/11/03/jak-uczyc-dziecko-czytac-sylabami-propozycje-zabaw/>

[^4]: Logopeda Nowicka, *Zabawy rytmiczne na podział na sylaby — logorytmika
  w domu i w przedszkolu*.
  <https://logopedanowicka.pl/zabawy-logorytmiczne-sylaby>

[^5]: Przedszkole Nr 9 w Nowej Soli, *Zabawy i ćwiczenia rozwijające słuch
  i uwagę słuchową*.
  <https://www.pp9nsol.eu/1769-zabawy-i-cwiczenia-rozwijajace-sluch-i-uwage-sluchowauwage-sluchowa-dziecka>

[^6]: Staffordshire County Council, *The 17 Early Learning Goals* (UK EYFS, PDF).
  <https://www.staffordshire.gov.uk/Education/Learning-options-and-careers/Getting-the-best-out-of-school/The-curriculum-and-what-your-child-will-learn/Documents/The-17-Early-Learning-Goals.pdf>

[^7]: U.S. Office of Head Start, *Interactive Head Start Early Learning
  Outcomes Framework: Ages Birth to Five*.
  <https://headstart.gov/interactive-head-start-early-learning-outcomes-framework-ages-birth-five>

[^8]: The Learning Agency, *Closing the Child Speech Recognition Gap:
  Evidence, Limitations, and Paths Forward*.
  <https://the-learning-agency.com/guides-resources/closing-the-child-speech-recognition-gap-evidence-limitations-and-paths-forward/>

[^9]: Unite.AI, *The Children's AI That Predates ChatGPT — Now Beating Google
  on Kids' Speech Recognition* (Buddy.ai case study).
  <https://www.unite.ai/buddy-ai-children-language-learning-speech-recognition/>

[^10]: BOLD Science (Jacobs Foundation), *Can conversational AI support
  children's wellbeing?*.
  <https://boldscience.org/can-conversational-ai-support-childrens-wellbeing/>

[^11]: International Dyslexia Association, *Building Phoneme Awareness: Know
  What Matters*.
  <https://dyslexiaida.org/building-phoneme-awareness-know-what-matters/>

[^12]: Reading Rockets, *Phonological and Phonemic Awareness: Activities for
  Your Pre-K Child*.
  <https://www.readingrockets.org/literacy-home/reading-101-guide-parents/your-pre-kindergarten-child/phonological-and-phonemic>

[^13]: SuperKid.pl, *Rymy i rymowanki*.
  <https://www.superkid.pl/rymy-i-rymowanki>

[^14]: Mamotoja.pl, *Rymowanki dla dzieci: krótkie i śmieszne*.
  <https://mamotoja.pl/male-dziecko/gry-i-zabawy/rymowanki-dla-dzieci-31065-r1/>

[^15]: Mamy-Mamom.pl, *Rymowanki dla dzieci*.
  <https://mamy-mamom.pl/rymowanki-dla-dzieci/>

[^16]: Przedszkole Nr 5 w Hajnówce, *Wesołe wierszyki i rymowanki dla dzieci*.
  <https://przedszkole5.hajnowka.pl/?p=17662>

[^17]: Języki Obce / blog, *Jakie dźwięki wydają zwierzęta w różnych językach*,
  plus *Trudności Przekładu — Co mówi lis?*.
  <https://www.jezykiobce.pl/blog/aktualnosci/jakie-dzwieki-wydaja-zwierzeta-w-roznych-jezykach-szczekanie-psa-miauczenie-kota-nie-tylko-po-angielsku>
  i <https://trudnosciprzekladu.wordpress.com/2017/05/08/co-mowi-lis/>

[^18]: Parenting Science, *Preschool number activities*.
  <https://parentingscience.com/preschool-number-activities/>

[^19]: NAEYC, *Playing Around with Number Composition: Games, Stories, and
  Everyday Problem Solving in the Preschool Classroom*.
  <https://www.naeyc.org/resources/pubs/tyc/spring2022/number-composition>

[^20]: Wikipedia, *I spy* (rules + age-appropriate variants).
  <https://en.wikipedia.org/wiki/I_spy>

[^21]: ReadWriteThink, *Guess What's in the Bag: A Language-based Activity*.
  <https://www.readwritethink.org/classroom-resources/lesson-plans/guess-what-language-based>

[^22]: Dzieciaki z Potencjałem, *Zgadnij jakie to zwierzę — zabawa dla
  przedszkolaków*, 2021.
  <https://dzieciakizpotencjalem.pl/2021/03/zgadnij-jakie-to-zwierze/>

[^23]: Education.com, *What's in the Bag? A Classification Game*.
  <https://www.education.com/activity/article/inthebag_preschool/>

[^24]: Playworks, *Game of the Week: Simon Says*.
  <https://www.playworks.org/resource/game-of-the-week-simon-says/>

[^25]: Childhood101, *How to Play Simon Says: Movement & Listening Game*.
  <https://childhood101.com/simon-says-movement-listening-game/>

[^26]: ASHA Leader, *Echoes of Language Development: 7 Facts About Echolalia
  for SLPs*.
  <https://leader.pubs.asha.org/do/10.1044/echoes-of-language-development-7-facts-about-echolalia-for-slps/full/>

[^27]: Speechy Musings, *Sequencing — Narrative Skill Development*.
  <https://speechymusings.com/topic/narratives/sequencing/>

[^28]: Dialogue with a conversational agent promotes children's story
  comprehension via enhancing engagement, *Child Development* (Oxford
  Academic / PMC), 2022.
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC9299009/>

[^29]: FirstCry Intelli, *Positional Words For Preschoolers*; LuxAI, *Step by
  step guide for teaching spatial prepositions to children with autism*.
  <https://www.firstcry.com/intelli/articles/positional-words-for-preschoolers/>
  and <https://luxai.com/blog/how-to-teach-spatial-prepositions-to-children-with-autism/>

[^30]: SuperKid.pl, *Wyszukaj słowa rozpoczynające się na (literę)*.
  <https://www.superkid.pl/slowa-rozpoczynajace-sie-na>

[^31]: Edukacja.edux.pl, *Metodyka wprowadzenia litery K*.
  <https://www.edukacja.edux.pl/p-39085-metodyka-wprowadzenia-litery-k.php>

[^32]: Przedszkole Nr 13, *Piosenki — „Głowa, ramiona, kolana, pięty"* (tekst
  + warianty).
  <http://www.p13.zsp3tg.iq.pl/grupy/piosenki-1/>

---

## Appendix A — Quick-reference table for implementation

| Activity      | Tool surface change                  | Pure prompt? | Memory key | Easiest age | Hardest age |
|---------------|--------------------------------------|--------------|------------|-------------|-------------|
| Klap sylaby   | `game_control: syl_start, stop`      | mostly       | `syl_tier` | 4           | 6           |
| Co się rymuje?| `game_control: rym_start, stop`      | mostly       | `rym_tier` | 4           | 6           |
| Co mówi zwierzę?| none                              | yes          | (none / `fav_animal`) | 3 | 5 |
| 20Q zwierzę   | `game_control: quiz_start, quiz_hint, stop` | partial | `quiz_pref` | 4 | 6 |
| Dokończ historię | none (reuse `create_animation`)   | yes          | `story_hero` | 4         | 6           |
| Karaoke       | none                                 | yes          | `song_fav`  | 3           | 6           |

## Appendix B — Why we did NOT pick the new tools we considered

| Considered new tool       | Verdict | Reason |
|---------------------------|---------|--------|
| `phonics_quiz`            | reject  | duplicates `game_control` shape; just extend enum |
| `say_word_to_screen({word})` | reject | adds a state-pipe between agent and browser; agent's voice already says the word, screen is just decoration |
| `score_streak({delta})`   | reject  | encourages competitive framing; against §7.3 |
| `pronounce({word})`       | reject  | redundant — agent's TTS is the pronunciation; no need to round-trip |
| `set_difficulty({tier})`  | reject  | tier lives in `remember`; no need for browser-side state |
| `assess_pronunciation({word, audio})` | reject | needs a separate ASR/phoneme model; out of scope, anti-§7.1 |

All of these would have been temptingly easy to add. None of them clear the
bar of *"the browser cannot do this without it"*. Keeping the surface tiny
keeps the agent prompts comprehensible and the cognitive overhead on the
LLM low.

## Appendix C — Skill-coverage matrix (do our 6 activities span the curriculum?)

| Skill (from §1.2)                 | A:sylaby | B:rymy | C:zwierzęta | D:20Q | E:historia | F:karaoke |
|-----------------------------------|:-:|:-:|:-:|:-:|:-:|:-:|
| Syllable segmentation              | ✅ | · | · | · | · | (✅ via rhythm) |
| Rhyme recognition                  | · | ✅ | · | · | · | ✅ |
| First-sound identification         | · | (✅) | (✅) | · | · | · |
| Vocabulary growth                  | · | · | ✅ | ✅ | ✅ | ✅ |
| Counting 1–10                      | · | · | · | · | (✅) | · |
| Listening / executive function     | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Narrative comprehension            | · | · | · | (✅) | ✅ | (✅) |
| Categorisation / inference         | · | · | (✅) | ✅ | · | · |
| Prosody / articulation             | ✅ | (✅) | ✅ | · | · | ✅ |
| Cultural literacy (Polish canon)   | · | ✅ | ✅ | · | · | ✅ |

Counting (F) is the most under-served — it's covered ad-hoc inside Historia
("smok zjadł **10** naleśników") but a standalone counting drill would round
out the offering. That's why §3.2 keeps F on the candidate list for a future
patch, even though it didn't make the v1 cut.

---

*End of brief. Total runtime estimate to implement v1 (the 4 enum additions +
prompt patches): ~2 dev-days. ~Zero data engineering. Ready for product
review.*
