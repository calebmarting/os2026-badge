# PlatformIO extra script: `pio run -t ota` builds, then updates every badge on
# the neck through the master's USB port (tools/ota_upload.py, src/ota.h).
# Pass --upload-port to pick the port, otherwise it is auto-detected.
import os

Import("env")

# Must match APP_MAX_SIZE in src/nvm.h: anything larger would overlap the
# staging area that an I2C update collects the new image in.
APP_MAX_SIZE = 0x1E000


def check_size(source, target, env):
    size = os.path.getsize(target[0].get_abspath())
    if size > APP_MAX_SIZE:
        print("Error: firmware is %d bytes, the I2C update limit is %d." % (size, APP_MAX_SIZE))
        env.Exit(1)


def ota_upload(source, target, env):
    tool = os.path.join(env.subst("$PROJECT_DIR"), "tools", "ota_upload.py")
    image = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    port = env.GetProjectOption("upload_port", None) or env.subst("$UPLOAD_PORT")
    cmd = '"$PYTHONEXE" "%s" "%s"' % (tool, image)
    if port:
        cmd += ' --port "%s"' % port
    return env.Execute(cmd)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)
env.AddCustomTarget(
    name="ota",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=[ota_upload],
    title="Update the neck",
    description="Send the firmware to the master over USB; it updates every badge over I2C",
)
