#!/bin/bash

APP_NAME="Minecraft_Mac_App"
LIB_NAME="libglfw.3.dylib"
HOMEBREW_LIB_PATH="/opt/homebrew/opt/glfw/lib/${LIB_NAME}"

# 1. Check if the executable exists
if [ ! -f "${APP_NAME}" ]; then
    echo "Error: ${APP_NAME} not found in the current directory."
    exit 1
fi

# 2. Check if the library is installed on the build machine
if [ ! -f "${HOMEBREW_LIB_PATH}" ]; then
    echo "Error: ${HOMEBREW_LIB_PATH} not found. Are you running this on the Mac that built it?"
    exit 1
fi

# 3. Copy the library into the same folder as the executable
echo "Copying ${LIB_NAME} to the current directory..."
cp "${HOMEBREW_LIB_PATH}" .

# 4. Give the copied library write permissions so we can modify it if needed
chmod +w "${LIB_NAME}"

# 5. Modify the executable to look for the library next to itself (@executable_path)
echo "Updating library paths in the executable..."
install_name_tool -change "${HOMEBREW_LIB_PATH}" "@executable_path/${LIB_NAME}" "${APP_NAME}"

echo "Done! You can now send the folder containing ${APP_NAME} and ${LIB_NAME} to your friend."
