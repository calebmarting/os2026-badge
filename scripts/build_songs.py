# PlatformIO extra script (pre): regenerates src/songs_data.cpp from
# songs/songs.json before every build, so dropping a MIDI file into songs/ and
# building is all a new easter egg needs. See tools/song_convert.py.
import os
import sys

Import("env")

project = env.subst("$PROJECT_DIR")
sys.path.insert(0, os.path.join(project, "tools"))
import song_convert  # noqa: E402

song_convert.build(os.path.join(project, "songs", "songs.json"), os.path.join(project, "src", "songs_data.cpp"))
