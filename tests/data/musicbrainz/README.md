# Fixtures for the MusicBrainz tests

Two answers from the MusicBrainz web service, saved on 2026-11-13 exactly as
they arrived. MusicBrainz core data is in the public domain (CC0).

- `release-search.json`: `GET /ws/2/release/?query=artist:"Radiohead" AND release:"OK Computer"&fmt=json&limit=3`
- `release.json`: `GET /ws/2/release/e8c3ff98-75f0-383f-a7d1-35db67661d89?inc=recordings+artist-credits+labels+release-groups&fmt=json`

The tests never contact the service.
