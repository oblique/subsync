subsync is a simple CLI tool that synchronizes SubRip (srt) subtitles
automatically. You only have to know when the first and the last
subtitle should be shown.

## examples

    subsync -f 00:01:33,492 -l 01:39:23,561 -i file.srt

In the above command the input is the `file.srt`. We set the first
subtitle to be shown at `00:01:33,492` and the last to be shown at
`01:39:23,561`. The synced file will be saved at `file.srt`.

    subsync -f 00:01:33,492 -l 01:39:23,561 -i file.srt -o newfile.srt

The above command is the same as the previous one, but the synced file
will be saved at `newfile.srt`.
