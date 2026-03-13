#!/bin/bash

APP_NAME="Minecraft_Mac_App"
LIB_NAME="libglfw.3.dylib"
GLEW_LIB_NAME="libGLEW.2.2.dylib"
HOMEBREW_LIB_PATH="$(brew --prefix)/opt/glfw/lib/${LIB_NAME}"
HOMEBREW_GLEW_PATH="$(brew --prefix)/opt/glew/lib/${GLEW_LIB_NAME}"

# 1. Check if the executable exists
if [ ! -f "${APP_NAME}" ]; then
    echo "Error: ${APP_NAME} not found in the current directory."
    exit 1
fi

# 2. Check if the libraries are installed on the build machine
if [ ! -f "${HOMEBREW_LIB_PATH}" ]; then
    echo "Error: ${HOMEBREW_LIB_PATH} not found. Are you running this on the Mac that built it?"
    exit 1
fi

if [ ! -f "${HOMEBREW_GLEW_PATH}" ]; then
    echo "Error: ${HOMEBREW_GLEW_PATH} not found. Are you running this on the Mac that built it?"
    exit 1
fi

# 3. Copy the libraries into the same folder as the executable
echo "Copying ${LIB_NAME} and ${GLEW_LIB_NAME} to the current directory..."
cp "${HOMEBREW_LIB_PATH}" .
cp "${HOMEBREW_GLEW_PATH}" .

# 4. Give the copied libraries write permissions so we can modify them if needed
chmod +w "${LIB_NAME}"
chmod +w "${GLEW_LIB_NAME}"

# 5. Modify the executable to look for the libraries next to itself (@executable_path)
echo "Updating library paths in the executable..."
install_name_tool -change "${HOMEBREW_LIB_PATH}" "@executable_path/${LIB_NAME}" "${APP_NAME}"
install_name_tool -change "${HOMEBREW_GLEW_PATH}" "@executable_path/${GLEW_LIB_NAME}" "${APP_NAME}"

echo "Done! You can now send the folder containing ${APP_NAME}, ${LIB_NAME}, and ${GLEW_LIB_NAME} to your friend."
