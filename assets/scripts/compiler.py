import subprocess
from xml.etree.ElementInclude import include

file_name = "app"
include_dir = "dependencies/include"
lib_dir = "dependencies/lib"
lib = "-lglfw3dll -lglew32s -lopengl32 -lgdi32"
preprocessor = "-DGLEW_STATIC"

def main():
    # include paths for generic project and imgui
    includes = f"-I {include_dir} -I dependencies/imgui -I dependencies/imgui/backends"
    
    # imgui source files we need to compile
    imgui_src = "dependencies/imgui/imgui.cpp dependencies/imgui/imgui_draw.cpp dependencies/imgui/imgui_tables.cpp dependencies/imgui/imgui_widgets.cpp dependencies/imgui/backends/imgui_impl_glfw.cpp dependencies/imgui/backends/imgui_impl_opengl2.cpp"
    
    # compile user scripts and imgui
    subprocess.call(f"g++ -c assets/scripts/main.cpp {imgui_src} {includes}")
    
    # link all object files together
    subprocess.call(f"g++ *.o -o {file_name} -L {lib_dir} {lib}")

if "__main__" == __name__:
    main()
