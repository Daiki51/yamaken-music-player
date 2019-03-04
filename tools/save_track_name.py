import os
import glob

os.chdir(os.path.dirname(os.path.abspath(__file__)))
os.chdir("../")

dir_path = "music_data/05 2019年2月ランキング/"

source_code = "char *track_names[] = {\n"

file_list = list(glob.glob(dir_path + "*"))

for i in range(0xff):
    filename = ""
    if i < len(file_list):
        file_path = file_list[i]
        filename = os.path.basename(file_path)
    source_code += "  \"{}\", \n".format(filename)

source_code += "};\n"

with open("src/YamakenMusicPlayer/tracknames.h", "wb") as f:
    f.write(source_code.encode("utf8"))
