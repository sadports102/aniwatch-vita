# aniwatch

A video player for the PS Vita, made for watching anime and movies.
It has **intro/outro skip**, **auto next episode** (also across seasons),
**resume** and a **Continue Watching** row, in a dark library UI with a purple accent.

> Version 0.1.0 · Title ID `AVWA00001` · personal project, use at your own risk

---

## Features

- **Library**: anime and movies with covers, banners, year, genres, rating and synopsis
- **Seasons**: season folders (`Season 1`, `S2`, `Specials`, ...) are merged into one show
- **Player**: hardware playback through `sceAvPlayer`, seek bar, volume, touch controls
- **Intro/outro skip**: mark the times once per episode, then skip them automatically or with a button
- **Auto next**: a countdown starts the next episode, also across seasons
- **Resume**: continues where you stopped (ask first or always resume)
- **Continue Watching**: the shows you're watching, at the top of the home screen
- **Online metadata**: search Kitsu (anime) and TMDB (movies) to download covers, banners and info
- **Touch support**: tap, double tap to seek, drag the seek bar

## Supported video

| | |
|---|---|
| Container | `.mp4` |
| Video | H.264, up to 960×544 |
| Audio | AAC |
| Subtitles | burned-in only (soft subtitles aren't supported) |

Convert other files to this format with HandBrake or ffmpeg, for example:

```
ffmpeg -i input.mkv -vf "scale=960:-2,subtitles=input.mkv" -c:v libx264 -profile:v main -crf 22 -c:a aac -b:a 128k output.mp4
```

## Installing

1. Install `aniwatch.vpk` with VitaShell.
2. Copy your videos to `ux0:data/aniwatch/` (see below).
3. Start aniwatch. Press **Triangle** on the home screen to rescan the library.

## Library layout

```
ux0:data/aniwatch/
├── anime/
│   └── Show Name/
│       ├── cover.jpg          poster (optional)
│       ├── banner.jpg         wide picture for the details screen (optional, or fanart.jpg)
│       ├── info.txt           show info (optional)
│       ├── Season 1/
│       │   ├── ep01.mp4
│       │   └── ep02.mp4
│       ├── Season 2/
│       └── Specials/
└── movies/
    ├── Movie Name/
    │   ├── movie.mp4
    │   ├── cover.jpg
    │   └── info.txt
    └── Another Movie.mp4      loose files work too
        Another Movie.jpg      (same name = cover)
```

- Number episodes with leading zeros (`ep01`, `ep02`, ... `ep10`) so they sort correctly.
- Season folders must be **inside** the show folder. A separate top-level
  folder like `Show Name Season 2` is shown as its own show.
- Episodes can also go straight into the show folder if there's only one season.

### info.txt

Plain text, one `key=value` per line. Every line is optional:

```
title=Show Name
year=2024
genres=Action, Fantasy
rating=8.4
type=TV
status=Finished
synopsis=One line of text describing the show.
```

You don't have to write these yourself: the online search on the details
screen fills in the cover, banner and `info.txt` for you.

## Controls

### Menus

| Button | Action |
|---|---|
| Left / right stick, D-pad | Move |
| X | Select |
| O | Back |
| L / R | Switch tabs |
| Triangle | Rescan library (home), hide item (Continue Watching) |
| Start | Open the side menu |

### Player

| Button | Action |
|---|---|
| X | Play / pause |
| O | Back to the library |
| D-pad left / right | Seek 10 s |
| D-pad up / down | Volume |
| Left stick | Scrub |
| L / R | Previous / next episode |
| Square | Intro/outro mark menu |

| Touch | Action |
|---|---|
| Tap | Show / hide the overlay |
| Double tap left / right side | Seek back / forward |
| Drag the seek bar | Seek |

## Intro and outro skip

1. While watching, press **Square** to open the mark menu.
2. Set **Intro start**, **Intro end** and **Outro start** at the current time.
   In the menu, the D-pad moves a mark by 1 s and L/R by 10 s.
3. Marks are saved per episode and shown as purple parts of the seek bar.

The **Skip intro/outro** setting decides what happens:

- **Auto**: skips without asking
- **Button**: shows a *Skip* button for a few seconds (default)
- **Off**: does nothing

Skipping the outro counts the episode as watched and starts auto next.

## Settings

| Setting | Options |
|---|---|
| Auto next | On / Off |
| Countdown | 5 / 10 / 15 s |
| Resume | Ask / Always |
| Skip intro/outro | Auto / Button / Off |
| Clear watch history | Clears progress and Continue Watching (skip marks are kept) |

## Saved data

Everything is stored in `ux0:data/aniwatch/`:

| File | Contents |
|---|---|
| `progress.txt` | Watch progress and hidden Continue Watching items |
| `skips.txt` | Intro/outro marks |
| `settings.txt` | Settings |
| `ca.pem` | Trimmed certificate list for HTTPS (rebuilt automatically) |
| `net_log.txt` | Network log, for troubleshooting |

## Building

Needs [VitaSDK](https://vitasdk.org) with these libraries (install with `vdpm`):
SDL2, SDL2_image, SDL2_ttf, curl, openssl, freetype, libpng, libjpeg-turbo, libwebp, zlib, bzip2.

```
git clone <this repo> aniwatch
cd aniwatch
./build.sh            # or: mkdir build && cd build && cmake .. && make
```

The result is `build/aniwatch.vpk`.

Files the build packs into the VPK:

- `res/font.ttf`, `res/font_bold.ttf`: the [Outfit](https://fonts.google.com/specimen/Outfit) font (SIL Open Font License)
- `res/cacert.pem`: CA certificate bundle for HTTPS

### LiveArea images

The LiveArea images aren't part of this repository. `make_livearea.py` builds them
(`icon0.png`, `bg.png`, `startup.png`, `template.xml` in `sce_sys/`) from your own
`mascot.png`. Without them the VPK still builds, but you need to remove the
`sce_sys` FILE lines from `CMakeLists.txt`.

## Credits

- Anime metadata from [Kitsu](https://kitsu.io)
- Movie metadata and images from [TMDB](https://www.themoviedb.org).
  *This product uses the TMDB API but is not endorsed or certified by TMDB.*
- [SDL2](https://www.libsdl.org), [libcurl](https://curl.se), [OpenSSL](https://www.openssl.org)
- VitaSDK and the PS Vita homebrew community

## License
