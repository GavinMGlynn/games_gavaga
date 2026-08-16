## What this changes

<!-- One or two sentences. Link an issue if there is one. -->

## Why

<!-- What problem does it solve, or what does it make possible? -->

## How it was checked

- [ ] Builds clean with `-DGAVAGA_WERROR=ON`
- [ ] Ran the sanitizer soak (`-DGAVAGA_ASAN=ON`, `--shot-at 20000`)
- [ ] Played it, or captured a frame with `--shot`

<!-- Screenshots welcome, especially for anything that moves or is drawn.
     SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
         ./build/gavaga --play --debug --shot frame.bmp --shot-at 1500 -->

## Notes

<!-- Anything a reviewer should know: trade-offs, things left undone,
     constants you tuned by eye. -->
