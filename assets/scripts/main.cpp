#include <iostream>
#include <cmath>
#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "object_loader.h"
#include "player.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../../dependencies/miniaudio.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

using namespace std;

GLuint loadTexture(const char* filepath) {
    int width, height, nrChannels;
    unsigned char *data = stbi_load(filepath, &width, &height, &nrChannels, 0);
    if (!data) {
        cout << "Failed to load texture: " << filepath << endl;
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLenum format = GL_RGB;
    if (nrChannels == 4) format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    return texture;
}

void getBlockPoints(Block b, float points[6][4][3]) {
    // We define all faces in Counter-Clockwise (CCW) order: Bottom-Left, Bottom-Right, Top-Right, Top-Left
    
    // Front face (Z = maxZ)
    points[0][0][0] = b.minX; points[0][0][1] = b.minY; points[0][0][2] = b.maxZ;
    points[0][1][0] = b.maxX; points[0][1][1] = b.minY; points[0][1][2] = b.maxZ;
    points[0][2][0] = b.maxX; points[0][2][1] = b.maxY; points[0][2][2] = b.maxZ;
    points[0][3][0] = b.minX; points[0][3][1] = b.maxY; points[0][3][2] = b.maxZ;
    // Back face (Z = minZ)
    points[1][0][0] = b.maxX; points[1][0][1] = b.minY; points[1][0][2] = b.minZ;
    points[1][1][0] = b.minX; points[1][1][1] = b.minY; points[1][1][2] = b.minZ;
    points[1][2][0] = b.minX; points[1][2][1] = b.maxY; points[1][2][2] = b.minZ;
    points[1][3][0] = b.maxX; points[1][3][1] = b.maxY; points[1][3][2] = b.minZ;
    // Top face (Y = maxY)
    points[2][0][0] = b.minX; points[2][0][1] = b.maxY; points[2][0][2] = b.maxZ;
    points[2][1][0] = b.maxX; points[2][1][1] = b.maxY; points[2][1][2] = b.maxZ;
    points[2][2][0] = b.maxX; points[2][2][1] = b.maxY; points[2][2][2] = b.minZ;
    points[2][3][0] = b.minX; points[2][3][1] = b.maxY; points[2][3][2] = b.minZ;
    // Bottom face (Y = minY)
    points[3][0][0] = b.minX; points[3][0][1] = b.minY; points[3][0][2] = b.minZ;
    points[3][1][0] = b.maxX; points[3][1][1] = b.minY; points[3][1][2] = b.minZ;
    points[3][2][0] = b.maxX; points[3][2][1] = b.minY; points[3][2][2] = b.maxZ;
    points[3][3][0] = b.minX; points[3][3][1] = b.minY; points[3][3][2] = b.maxZ;
    // Left face (X = minX)
    points[4][0][0] = b.minX; points[4][0][1] = b.minY; points[4][0][2] = b.minZ;
    points[4][1][0] = b.minX; points[4][1][1] = b.minY; points[4][1][2] = b.maxZ;
    points[4][2][0] = b.minX; points[4][2][1] = b.maxY; points[4][2][2] = b.maxZ;
    points[4][3][0] = b.minX; points[4][3][1] = b.maxY; points[4][3][2] = b.minZ;
    // Right face (X = maxX)
    points[5][0][0] = b.maxX; points[5][0][1] = b.minY; points[5][0][2] = b.maxZ;
    points[5][1][0] = b.maxX; points[5][1][1] = b.minY; points[5][1][2] = b.minZ;
    points[5][2][0] = b.maxX; points[5][2][1] = b.maxY; points[5][2][2] = b.minZ;
    points[5][3][0] = b.maxX; points[5][3][1] = b.maxY; points[5][3][2] = b.maxZ;
}

int main(void)
{
    GLFWwindow* window;

    if (!glfwInit()) {
        return -1;
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(1920, 1080, "Minecraft 2.0!", NULL, NULL);

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);

    // Enable backface culling (don't draw the inside of faces)
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);   // Front faces are drawn counter-clockwise (default)
    glCullFace(GL_BACK);   // Cull the back faces

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // Get actual window size for proper scaling
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    float ratio = (float)width / (float)height;
    
    glFrustum(-ratio * 0.1f, ratio * 0.1f, -0.1f, 0.1f, 0.1f, 100.0f); // 3D Camera Lens
    glMatrixMode(GL_MODELVIEW);
    
    // Set sky color (Light Blue) so it doesn't look like we're in space!
    glClearColor(0.3f, 0.5f, 0.92f, 1.0f);
    
    // Create variables to represent our player's 3D position
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerZ = 0.0f;
    
    // Now speeds are per SECOND instead of per FRAME
    float speed = 2.0f;       // 2 units per second

    // Physics variables
    float playerVelocityY = 0.0f;
    float gravity = 15.0f;
    float jumpVelocity = 6.2f;
    float playerHeight = 1.5f; // Eye level above ground
    float playerRadius = 0.2f; // Player hit box radius
    bool isOnGround = false;

    // Define Level Blocks Dynamically
    std::vector<Block> level;

    // Create variables representing player's rotation (Camera angle)
    float playerYaw = 0.0f;   // Looking left/right (rotating around Y axis)
    float playerPitch = 0.0f; // Looking up/down (rotating around X axis)
    float mouseSensitivity = 0.1f;    // Mouse sensitivity

    // DeltaTime Tracking
    float lastFrameTime = glfwGetTime();

    GLuint grassTex = loadTexture("assets/textures/grass.png");
    GLuint dirtTex = loadTexture("assets/textures/dirt.png");
    GLuint stoneTex = loadTexture("assets/textures/stone.png");
    GLuint oak_planksTex = loadTexture("assets/textures/oak_planks.png");

    GLuint currentSelectedTexture = grassTex;

    int floorSize = 50;
    const int blockSize = 1;

    // Generate the floor grid!
    // We want the floor to be centered at (0,0), so we start from -floorSize/2 to +floorSize/2
    float startCoord = -((float)floorSize * blockSize) / 2.0f;
    
    for (int x = 0; x < floorSize; ++x) {
        for (int z = 0; z < floorSize; ++z) {
            float curX = startCoord + (x * blockSize);
            float curZ = startCoord + (z * blockSize);
            
            Block newBlock = {
                curX, curX + blockSize,      // minX, maxX
                -1.0f, -0.0f,                // minY, maxY (floor is 1 unit thick)
                curZ, curZ + blockSize,      // minZ, maxZ
                grassTex                     // textureID
            };
            
            level.push_back(newBlock);
        }
    }

    for (int y = -1; y >= -3; --y) {
        for (int x = 0; x < floorSize; ++x) {
            for (int z = 0; z < floorSize; ++z) {
                float curX = startCoord + (x * blockSize);
                float curZ = startCoord + (z * blockSize);
                
                Block newBlock = {
                    curX, curX + blockSize,      // minX, maxX
                    (float)y - 1.0f, (float)y,   // minY, maxY
                    curZ, curZ + blockSize,      // minZ, maxZ
                    dirtTex                      // textureID
                };
                
                level.push_back(newBlock);
            }
        }
    }

    for (int y = -4; y >= -7; --y) {
        for (int x = 0; x < floorSize; ++x) {
            for (int z = 0; z < floorSize; ++z) {
                float curX = startCoord + (x * blockSize);
                float curZ = startCoord + (z * blockSize);
                
                Block newBlock = {
                    curX, curX + blockSize,      // minX, maxX
                    (float)y - 1.0f, (float)y,   // minY, maxY
                    curZ, curZ + blockSize,      // minZ, maxZ
                    stoneTex                      // textureID
                };
                
                level.push_back(newBlock);
            }
        }
    }
    
    int NUM_BLOCKS = level.size();

    // Hide the mouse cursor and lock it to the center of the window!
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    // Keep track of where the mouse was last frame
    double lastMouseX, lastMouseY;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

    bool leftMousePressed = false;
    bool rightMousePressed = false;
    float lastPlaceTime = 0.0f;
    float lastBreakTime =  0.0f;
    float placeCooldown = 0.15f;
    float breakCooldown = 0.8f; // Temporary value

    // FPS tracking
    int fpsFrames = 0;
    float fpsLastTime = glfwGetTime();
    int lastFps = 0;
    std::string blockName = "Grass";

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    ma_engine engine;
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
        cout << "Failed to initialize audio engine." << endl;
    }

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(window))
    {
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- FPS CALCULATION ---
        fpsFrames++;
        float currentFpsTime = glfwGetTime();
        if (currentFpsTime - fpsLastTime >= 1.0f) {
            lastFps = fpsFrames;
            fpsFrames = 0;
            fpsLastTime = currentFpsTime;
        }

        // --- DELTA TIME CALCULATION ---
        float currentFrameTime = glfwGetTime();
        float deltaTime = currentFrameTime - lastFrameTime; // How much time passed since last loop?
        lastFrameTime = currentFrameTime;

        player_movement_update(window, playerX, playerY, playerZ, 
                               playerVelocityY, playerYaw, playerPitch, 
                               speed, gravity, jumpVelocity, 
                               playerHeight, playerRadius, isOnGround,
                               deltaTime, lastMouseX, lastMouseY, 
                               mouseSensitivity, level.data(), NUM_BLOCKS, &engine);

        // --- HOTBAR SELECTION ---
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && currentSelectedTexture != grassTex) {
            currentSelectedTexture = grassTex;
            blockName = "Grass";
        }
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && currentSelectedTexture != dirtTex) {
            currentSelectedTexture = dirtTex;
            blockName = "Dirt";
        }
        if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && currentSelectedTexture != stoneTex) {
            currentSelectedTexture = stoneTex;
            blockName = "Stone";
        }
        if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS && currentSelectedTexture != oak_planksTex) {
            currentSelectedTexture = oak_planksTex;
            blockName = "Oak Planks";
        }

        // --- MOUSE CLICK INTERACTION ---
        bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (leftDown && (currentFrameTime - lastBreakTime >= breakCooldown)) {
            float placeX, placeY, placeZ;
            int hitIndex = raycast(playerX, playerY, playerZ, playerPitch, playerYaw, level.data(), NUM_BLOCKS, 5.0f, 0.05f, placeX, placeY, placeZ);
            if (hitIndex != -1) {
                GLuint brokenTexture = level[hitIndex].textureID;
                level.erase(level.begin() + hitIndex);
                NUM_BLOCKS = level.size();

                if (brokenTexture == grassTex || brokenTexture == dirtTex) {
                    ma_engine_play_sound(&engine, "assets/sounds/dirt_break.wav", NULL);
                } else if (brokenTexture == stoneTex) {
                    ma_engine_play_sound(&engine, "assets/sounds/stone_break.wav", NULL);
                } else if (brokenTexture == oak_planksTex) {
                    ma_engine_play_sound(&engine, "assets/sounds/wood_break.wav", NULL);
                } else {
                    ma_engine_play_sound(&engine, "assets/sounds/default_break.wav", NULL);
                }
                
                lastBreakTime = currentFrameTime;
            }
        }
        leftMousePressed = leftDown;

        bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rightDown && (currentFrameTime - lastPlaceTime >= placeCooldown)) {
            float placeX, placeY, placeZ;
            int hitIndex = raycast(playerX, playerY, playerZ, playerPitch, playerYaw, level.data(), NUM_BLOCKS, 5.0f, 0.05f, placeX, placeY, placeZ);
            if (hitIndex != -1) {
                // Determine placement based on grid alignment (rounding to nearest blockSize boundary)
                float gridX = floor(placeX / blockSize) * blockSize;
                float gridY = floor(placeY / blockSize) * blockSize; // Might need special tweak if floor is Y: -1 to 0
                float gridZ = floor(placeZ / blockSize) * blockSize;

                // Adjust gridY if it's the floor, since floor blocks are 1 unit thick but go from -1 to 0
                // For simplicity, let's just make new blocks 1x1x1
                Block newBlock = {
                    gridX, gridX + blockSize,
                    gridY, gridY + 1.0f, // Make placed blocks 1 unit tall
                    gridZ, gridZ + blockSize,
                    currentSelectedTexture
                };
                
                if (!isColliding(playerX, playerY, playerZ, playerRadius - 0.1f, playerHeight, newBlock)) {
                    level.push_back(newBlock);
                    NUM_BLOCKS = level.size();
                    lastPlaceTime = currentFrameTime;
                }
            }
        }
        rightMousePressed = rightDown;

        float outlineColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float defaultModelColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};

        // Allow user to press ESCAPE to close window (since they can't click the X anymore!)
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float lightPos[] = { 10.0f, 20.0f, 10.0f, 1.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        glPushMatrix();

        glRotatef(-playerPitch, 1.0f, 0.0f, 0.0f); // Look up/down (Tilt world X-axis)
        glRotatef(-playerYaw,   0.0f, 1.0f, 0.0f); // Look left/right (Spin world Y-axis)

        glTranslatef(-playerX, -playerY, -playerZ);

        // Draw all blocks in the level array!
        for (int i = 0; i < NUM_BLOCKS; ++i) {
            float tempPoints[6][4][3];
            getBlockPoints(level[i], tempPoints);
            // We pass "false" to disable outlines for performance
            draw_cube_texture(level[i].textureID, tempPoints, outlineColor, 1.0f, false, true);
        }

        // Reset the world back to normal so the next frame starts clean
        glPopMatrix();

        // --- DRAW CROSSHAIR (2D Orthographic Projection) ---
        int winWidth, winHeight;
        glfwGetFramebufferSize(window, &winWidth, &winHeight);
        
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, winWidth, winHeight, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // White crosshair
        glLineWidth(4.0f);
        
        float centerX = winWidth / 2.0f;
        float centerY = winHeight / 2.0f;
        
        glBegin(GL_LINES);
        // Horizontal line
        glVertex2f(centerX - 20, centerY);
        glVertex2f(centerX + 20, centerY);
        // Vertical line
        glVertex2f(centerX, centerY - 20);
        glVertex2f(centerX, centerY + 20);
        glEnd();
        glLineWidth(1.0f);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        // Render ImGui UI on top of everything
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(250, 70), ImGuiCond_Always);
        
        // We can optionally add flags like ImGuiWindowFlags_NoTitleBar if you don't want a top bar at all!
        ImGui::Begin("HUD", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        ImGui::Text("FPS: %d, Selected: %s", lastFps, blockName.c_str());
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    // Cleanup ImGui
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    ma_engine_uninit(&engine);

    glfwTerminate();
    return 0;
}