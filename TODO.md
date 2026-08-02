# TODO

## Bugs
- [ ] Bug: Raspberry Pi 4 has issues with serving detail pages. May be internet, py read / write speed, pi network stack or src code itself. 

## Logic
- [ ] Refactor sourcing for modularity and security (with possible distribution in mind)
- [x] De-dupe for sources to save on AI querries and dont show the user doubles
- [ ] Onboarding questions not optimal, also tedious to fill out. Maybe pick questions / answers

### De-dupe: shipped 2026-08-02

`dupe_key = company|title|city`, diacritic-folded and lowercased, written on every path
that can change those fields and swept at every start by `refresh_dupe_keys`. One card
per group, chosen at read time by `initial_publication_date DESC, job_id`.

The attempt-1 post-mortem that used to sit here was wrong about why it failed and has
been deleted. Its headline claim — VAT Vakuumventile has "three real `Product Quality
Engineer` postings in Haag" — does not hold: those rows are re-posts of one opening with
byte-identical bodies. The alternative it proposed, a signal from the posting body, was
tested and ranks the wrong way round: true cross-source duplicates sit at Jaccard
0.48–0.53 because the two boards wrap the same job in different boilerplate, while
same-source pairs sit at 0.63–0.97. No threshold separates them. Header fields win.

The two failures that were real are fixed by mirroring instead of grouping:
- **State does not die with the twin.** Every member row carries the same `user_status`,
  rating, notes, tracker fields and fit result, so it does not matter which member
  `delete_expired_jobs` hard-deletes first — verified by expiring the jobs.ch member of a
  cross-source pair and finding the LinkedIn survivor still holding all of it.
- **No arbitrary representative.** The newest listing represents the group, and since
  state is mirrored it costs nothing when that changes. The other listings are reachable
  from the detail panel, each with its own link and end date.

Mirroring also removed the need for a group table, a canonical-row pointer and promotion
logic — every existing read query kept working.

On the 1339-row dev snapshot: 106 groups, 21 spanning both sources, largest 8, 6 keyless
rows that never group; 1339 rows collapse to 1194 cards, 152 visible to 141. The AI saving
is real and measured: the fitcheck batch query collapses too (41 eligible rows → 37
openings), and one fitcheck on a VAT row scored all three members in a single call.

Known limit, accepted: Sensirion has a DE and an EN version of one `Hardware Engineer`
@Stäfa posting plus a genuinely different PCB-layout role. They merge and the third
inherits a sibling's score. The `×N` badge and the listings block are the escape hatch —
nothing becomes unreachable. No split action, deliberately.

## UX improvements
- [ ] Overall further simplify UI


## Ideas
- Data sources and more scraping
- Adding a general AI ask me questions logic to ask questions like, does A fit better than B, or maybe even adjust rating due to reason X.
- Reminder system and generally full pipeline from searching to apply overview, reminders and more
- .exe compilation probably not needed. But this results in a general user use-case overhaul



# The Project and its main Issue

Project

Self-hosted job application tool. Raspberry Pi, SQLite, AGPL-3.0. Two users: you and a friend. Built for your own use; not laymen-ready.

Two parts:
- Ingest — mass scrapes jobs.ch and LinkedIn into the DB.
- Pipeline — save, rate, fitcheck, apply notes.

Known facts

- jobs.ch: robots.txt disallows the endpoint in use. No guards against mass scraping beyond DDoS protection.
- LinkedIn: scraped via public guest pages, no account. 25 results per query currently.
- LinkedIn carries jobs found nowhere else, and higher quality than alternatives.
- Combined ingest surfaces more jobs than professional Swiss job agencies.
- Pipeline is standalone valuable — usable with no ingest, rating jobs the user encounters. Mass data is a multiplier on it, not a prerequisite.
- Scraping is not sellable. Pipeline is.
- Centralised ingest amortises: one scrape serves any number of users.
- /api/jobs measured 1.80s TTFB, 5.42s total, 13.1MB before the -O3+gzip and soft-delete patches. Not re-measured since.

The distribution problem

Both data sources are unlicensed and used against their ToS. No arrangement discussed removes this while keeping coverage

Client-side additionally needs the user's LinkedIn account and their browser open.

The legal configurations areow what the tool currently is for you. The gap between them and the current tool is the mass ingest.

Not known

- Whether JobCloud would license jobs.ch access. Never asked.
- Coverage loss if LinkedIn were dropped — no number.
- Whether a LinkedIn account
- Your actual legal exposure, at any of these configurations. Everything I said about Swiss database rights, UWG, DSG and US case law was reasoning from general knowledge, not verified for your situation. It needs a lawyer, and the answer plausibly decides which configurations are ope

## Direction / Way Forward

Every big decision (data model, license, money, JobCloud pitch) is blocked on missing info — coverage numbers, legal exposure, a roadmap to pitch. But one thing is valuable under *every* outcome and blocked by *none* of the unknowns: making the pipeline production-ready (installable by a stranger). It's required for OSS release, for a pitch, for a side hustle, and for the portfolio to not look like a toy. So it's not blocked — it's the actual next step. Undecided things have been stalling the decided thing.

Wants, mapped:

- **Help people, not corporates** — AGPL already prevents closed-source corporate takeover. To go further: PolyForm Noncommercial or dual-license (individuals free, companies pay). Decide later; blocks nothing now.
- **Get something back beyond "it exists"** — the lackluster feeling is real because nobody but you touches it. The return is a stranger using it and saying so, which outweighs stars. Requires installable-by-strangers. Same unblock.
- **Legally okay** — already green for current use (self-host, own use). Stays green as long as you don't host scraping for others. Ship pipeline + legal adapters only; scrapers stay private setup.
- **Side hustle** — hardest, needs most info, decide last. Production-readiness is a prerequisite anyway. Build first, choose money model once users + coverage numbers exist.

Sequence (no unknowns required):

1. Harden pipeline to installable-by-a-stranger. Close the "not laymen-ready" gap: config, docs, setup.
2. Ship with legal adapters only (job-room, career pages, official APIs). Scrapers stay private plugins.
3. Release AGPL or PolyForm-NC. Real README, one screenshot, honest scope.
4. Coverage benchmarking now has a point — real users hit real gaps and report which sources they miss. The tedious test becomes their bug reports.
5. With users + coverage data + working product, the JobCloud pitch writes itself and the money-model question has answers.

Reframe: deciding the endgame before the opening. Every named unknown gets cheaper to answer *after* steps 1–3, none before. Stop deciding, ship the pipeline, let real usage resolve the unknowns.
