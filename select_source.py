import os
import shutil

Import("env")

env_name = env["PIOENV"]
src_dir = os.path.join(env["PROJECT_DIR"], "src")
sketch_dir = os.path.join(env["PROJECT_DIR"], "m5_script")
sketch_file = f"m5_petit_{env_name}.ino"
sketch_path = os.path.join(sketch_dir, sketch_file)

if not os.path.exists(sketch_path):
    print(f"ERROR: Sketch not found: {sketch_path}")
    env.Exit(1)

os.makedirs(src_dir, exist_ok=True)

dest = os.path.join(src_dir, sketch_file)
shutil.copy2(sketch_path, dest)
print(f"Selected source: {sketch_file} -> src/")
