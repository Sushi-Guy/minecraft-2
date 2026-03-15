#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "rendering.h"
#include "player.h"
#include "interactions.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../../dependencies/miniaudio.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "nakama_client.h"

using namespace std;

enum GameState { STATE_MAIN_MENU, STATE_IN_GAME };

int main(void)
{
    // Initialize Nakama Client
    NakamaClient nakamaClient("defaultkey", "127.0.0.1", 7350);
    NakamaSession nakamaSession;
    bool nakamaAuthPending = false; // Flag to handle auth on the main thread
    float lastPositionSendTime = 0.0f;
    float positionSendInterval = 0.1f; // Send position 10 times per second
    float lastPositionPollTime = 0.0f;
    float positionPollInterval = 0.15f; // Poll for others 6-7 times per second
    float lastBlockEventPollTime = 0.0f;
    float blockEventPollInterval = 0.2f; // Poll for block changes 5 times per second

    // Block events from remote players, applied on main thread
    std::mutex blockEventMutex;
    std::vector<BlockEvent> pendingBlockEvents;

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
    float playerRadius = 0.3f; // Player hit box radius
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

    // Texture ID helpers: OpenGL texture handles differ between instances,
    // so we use a stable 0-3 integer for network serialization.
    auto texToNetId = [&](GLuint tex) -> int {
        if (tex == grassTex)     return 0;
        if (tex == dirtTex)      return 1;
        if (tex == stoneTex)     return 2;
        if (tex == oak_planksTex) return 3;
        return 0; // default
    };
    auto netIdToTex = [&](int id) -> GLuint {
        switch (id) {
            case 1: return dirtTex;
            case 2: return stoneTex;
            case 3: return oak_planksTex;
            default: return grassTex;
        }
    };

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
                grassTex,                    // textureID
                true                         // isActive
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
                    dirtTex,                     // textureID
                    true                         // isActive
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
                    stoneTex,                     // textureID
                    true                         // isActive
                };
                
                level.push_back(newBlock);
            }
        }
    }
    
    int NUM_BLOCKS = level.size();


    const int CHUNK_SIZE = 512;
    std::vector<RenderChunk> chunks;
    
    // Generate initial chunks
    for (int i = 0; i < NUM_BLOCKS; i += CHUNK_SIZE) {
        RenderChunk chunk;
        chunk.listId = glGenLists(1);
        chunk.dirty = true;
        chunk.startIdx = i;
        chunk.endIdx = std::min(i + CHUNK_SIZE, NUM_BLOCKS);
        chunks.push_back(chunk);
    }

    // The cursor is visible in the main menu!
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    // Keep track of where the mouse was last frame
    double lastMouseX, lastMouseY;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

    bool leftMousePressed = false;
    bool rightMousePressed = false;
    float lastPlaceTime = 0.0f;
    float currentBreakTime = 0.0f;
    float lastBreakSoundTime = 0.0f;
    float lastBreakTime = 0.0f;
    float breakCooldown = 0.25f;
    int currentBreakingBlockIndex = -1;
    float placeCooldown = 0.2f;

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

    // Setup Display List for Optimization
    int lastBreakingBlockIndex = -1;

    GameState currentState = STATE_MAIN_MENU;

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(window))
    {
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Nakama uses background threads, no tick needed

        // Check if Nakama auth completed (must handle GLFW calls on main thread)
        if (nakamaAuthPending) {
            nakamaAuthPending = false;
            currentState = STATE_IN_GAME;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            lastFrameTime = glfwGetTime();

            // Join multiplayer: start sending/receiving positions via storage
            nakamaClient.inMatch = true;
            std::cout << "Multiplayer active! Your user ID: " << nakamaSession.userId << std::endl;
        }

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

        if (currentState == STATE_IN_GAME) {
            // Check for pause
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                currentState = STATE_MAIN_MENU;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }

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
        handleMouseInteractions(window, currentFrameTime, deltaTime, lastBreakTime, breakCooldown, 
            currentBreakingBlockIndex, currentBreakTime, lastBreakSoundTime, lastPlaceTime, placeCooldown,
            playerX, playerY, playerZ, playerPitch, playerYaw, playerRadius, playerHeight,
            level, NUM_BLOCKS, chunks, CHUNK_SIZE,
            grassTex, dirtTex, stoneTex, oak_planksTex, currentSelectedTexture,
            &engine, blockSize, leftMousePressed, rightMousePressed,
            // onBlockBreak
            [&](int idx) { nakamaClient.sendBlockBreak(nakamaSession, idx); },
            // onBlockPlace
            [&](float minX, float maxX, float minY, float maxY, float minZ, float maxZ, int texId) {
                nakamaClient.sendBlockPlace(nakamaSession, minX, maxX, minY, maxY, minZ, maxZ, texToNetId((GLuint)texId));
            }
        );

        // --- MULTIPLAYER: Send our position, poll for others, poll block events ---
        if (nakamaSession.isValid && nakamaClient.inMatch) {
            if (currentFrameTime - lastPositionSendTime >= positionSendInterval) {
                nakamaClient.sendPosition(nakamaSession, playerX, playerY, playerZ, playerYaw, playerPitch);
                lastPositionSendTime = currentFrameTime;
            }
            if (currentFrameTime - lastPositionPollTime >= positionPollInterval) {
                nakamaClient.pollPositions(nakamaSession);
                lastPositionPollTime = currentFrameTime;
            }
            if (currentFrameTime - lastBlockEventPollTime >= blockEventPollInterval) {
                nakamaClient.pollBlockEvents(nakamaSession, [&](const BlockEvent& ev) {
                    std::lock_guard<std::mutex> lock(blockEventMutex);
                    pendingBlockEvents.push_back(ev);
                });
                lastBlockEventPollTime = currentFrameTime;
            }
        }

        // --- Apply pending block events on the main thread ---
        {
            std::lock_guard<std::mutex> lock(blockEventMutex);
            for (const auto& ev : pendingBlockEvents) {
                if (ev.type == "break") {
                    if (ev.index >= 0 && ev.index < NUM_BLOCKS) {
                        level[ev.index].isActive = false;
                        chunks[ev.index / CHUNK_SIZE].dirty = true;
                    }
                } else if (ev.type == "place") {
                    Block newBlock = { ev.minX, ev.maxX, ev.minY, ev.maxY, ev.minZ, ev.maxZ,
                                      netIdToTex(ev.textureId), true };
                    // Find a free slot or append
                    int placedIndex = -1;
                    for (int i = 0; i < NUM_BLOCKS; i++) {
                        if (!level[i].isActive) { level[i] = newBlock; placedIndex = i; break; }
                    }
                    if (placedIndex == -1) {
                        level.push_back(newBlock);
                        placedIndex = NUM_BLOCKS++;
                        if (!chunks.empty() && (chunks.back().endIdx - chunks.back().startIdx) < CHUNK_SIZE) {
                            chunks.back().endIdx++;
                        } else {
                            RenderChunk nc; nc.listId = glGenLists(1); nc.dirty = true;
                            nc.startIdx = placedIndex; nc.endIdx = placedIndex + 1;
                            chunks.push_back(nc);
                        }
                    }
                    chunks[placedIndex / CHUNK_SIZE].dirty = true;
                }
            }
            pendingBlockEvents.clear();
        }
        }

        float outlineColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float defaultModelColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (currentState == STATE_IN_GAME) {
            float lightPos[] = { 10.0f, 20.0f, 10.0f, 1.0f };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        glPushMatrix();

        glRotatef(-playerPitch, 1.0f, 0.0f, 0.0f); // Look up/down (Tilt world X-axis)
        glRotatef(-playerYaw,   0.0f, 1.0f, 0.0f); // Look left/right (Spin world Y-axis)

        glTranslatef(-playerX, -playerY, -playerZ);

        // Removed: We no longer recompile the whole world just to change the targeted block

        // Recompile only the dirty chunks
        for (auto& chunk : chunks) {
            if (chunk.dirty) {
                glNewList(chunk.listId, GL_COMPILE);
                for (int i = chunk.startIdx; i < chunk.endIdx; ++i) {
                    if (!level[i].isActive) continue;
                    
                    float tempPoints[6][4][3];
                    getBlockPoints(level[i], tempPoints);
                    draw_cube_texture(level[i].textureID, tempPoints, outlineColor, 1.0f, false, true, nullptr);
                }
                glEndList();
                chunk.dirty = false;
            }
        }

        // Draw all the chunks
        for (const auto& chunk : chunks) {
            glCallList(chunk.listId);
        }

        // Draw the block currently being broken, with its updated color
        if (currentBreakingBlockIndex != -1 && currentBreakingBlockIndex < NUM_BLOCKS && level[currentBreakingBlockIndex].isActive) {
            glDepthFunc(GL_LEQUAL); // Allows this drawing to perfectly overwrite the existing block
            int i = currentBreakingBlockIndex;
            float tempPoints[6][4][3];
            getBlockPoints(level[i], tempPoints);
            
            float blockColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            float requiredBreakTime = 0.5f;
            GLuint tex = level[i].textureID;
            if (tex == grassTex || tex == dirtTex) {
                requiredBreakTime = 0.5f;
            } else if (tex == oak_planksTex) {
                requiredBreakTime = 1.0f;
            } else if (tex == stoneTex) {
                requiredBreakTime = 1.5f;
            }
            
            float progress = currentBreakTime / requiredBreakTime;
            if (progress > 1.0f) progress = 1.0f;
            
            float shade = 1.0f - (progress * 0.7f); // Drops down to 0.3 brightness
            blockColor[0] = shade;
            blockColor[1] = shade;
            blockColor[2] = shade;
            
            draw_cube_texture(level[i].textureID, tempPoints, outlineColor, 1.0f, false, true, blockColor);
            
            glDepthFunc(GL_LESS); // Restore to default
        }

        // --- DRAW OTHER PLAYERS ---
        {
            auto players = nakamaClient.getRemotePlayers();
            for (const auto& pair : players) {
                const RemotePlayer& rp = pair.second;
                glPushMatrix();
                // Position the player (feet position, offset Y down by half the player height)
                glTranslatef(rp.x, rp.y - 0.75f, rp.z);
                
                // Draw a simple colored box to represent the player
                float playerWidth = 0.4f;
                float playerBodyHeight = 1.5f;
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.2f, 0.6f, 1.0f); // Blue player color
                
                glBegin(GL_QUADS);
                // Front
                glVertex3f(-playerWidth, 0, playerWidth);
                glVertex3f(playerWidth, 0, playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, playerWidth);
                glVertex3f(-playerWidth, playerBodyHeight, playerWidth);
                // Back
                glVertex3f(playerWidth, 0, -playerWidth);
                glVertex3f(-playerWidth, 0, -playerWidth);
                glVertex3f(-playerWidth, playerBodyHeight, -playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, -playerWidth);
                // Left
                glVertex3f(-playerWidth, 0, -playerWidth);
                glVertex3f(-playerWidth, 0, playerWidth);
                glVertex3f(-playerWidth, playerBodyHeight, playerWidth);
                glVertex3f(-playerWidth, playerBodyHeight, -playerWidth);
                // Right
                glVertex3f(playerWidth, 0, playerWidth);
                glVertex3f(playerWidth, 0, -playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, -playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, playerWidth);
                // Top
                glVertex3f(-playerWidth, playerBodyHeight, playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, playerWidth);
                glVertex3f(playerWidth, playerBodyHeight, -playerWidth);
                glVertex3f(-playerWidth, playerBodyHeight, -playerWidth);
                // Bottom
                glVertex3f(-playerWidth, 0, -playerWidth);
                glVertex3f(playerWidth, 0, -playerWidth);
                glVertex3f(playerWidth, 0, playerWidth);
                glVertex3f(-playerWidth, 0, playerWidth);
                glEnd();
                
                // Draw a head (smaller cube on top)
                glColor3f(0.9f, 0.7f, 0.5f); // Skin-ish color
                float headSize = 0.3f;
                float headY = playerBodyHeight;
                glBegin(GL_QUADS);
                // Front
                glVertex3f(-headSize, headY, headSize);
                glVertex3f(headSize, headY, headSize);
                glVertex3f(headSize, headY + headSize*2, headSize);
                glVertex3f(-headSize, headY + headSize*2, headSize);
                // Back
                glVertex3f(headSize, headY, -headSize);
                glVertex3f(-headSize, headY, -headSize);
                glVertex3f(-headSize, headY + headSize*2, -headSize);
                glVertex3f(headSize, headY + headSize*2, -headSize);
                // Left
                glVertex3f(-headSize, headY, -headSize);
                glVertex3f(-headSize, headY, headSize);
                glVertex3f(-headSize, headY + headSize*2, headSize);
                glVertex3f(-headSize, headY + headSize*2, -headSize);
                // Right
                glVertex3f(headSize, headY, headSize);
                glVertex3f(headSize, headY, -headSize);
                glVertex3f(headSize, headY + headSize*2, -headSize);
                glVertex3f(headSize, headY + headSize*2, headSize);
                // Top
                glVertex3f(-headSize, headY + headSize*2, headSize);
                glVertex3f(headSize, headY + headSize*2, headSize);
                glVertex3f(headSize, headY + headSize*2, -headSize);
                glVertex3f(-headSize, headY + headSize*2, -headSize);
                glEnd();
                
                glPopMatrix();
            }
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
        }

        // Render ImGui UI on top of everything
        if (currentState == STATE_IN_GAME) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(250, 70), ImGuiCond_Always);
            
            // We can optionally add flags like ImGuiWindowFlags_NoTitleBar if you don't want a top bar at all!
            ImGui::Begin("HUD", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
            auto players = nakamaClient.getRemotePlayers();
            ImGui::Text("FPS: %d, Selected: %s, Players: %d", lastFps, blockName.c_str(), (int)players.size() + 1);
            ImGui::End();
        } else if (currentState == STATE_MAIN_MENU) {
            int winW, winH;
            glfwGetFramebufferSize(window, &winW, &winH);
            
            ImVec2 window_size = ImVec2(300, 200);
            ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2((winW - window_size.x) * 0.5f, (winH - window_size.y) * 0.5f), ImGuiCond_Always);
            
            ImGui::Begin("Main Menu", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
            
            ImVec2 title_size = ImGui::CalcTextSize("Minecraft 2.0");
            ImGui::SetCursorPosX((window_size.x - title_size.x) * 0.5f);
            ImGui::Text("Minecraft 2.0");
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            float btn_w = 200.0f;
            ImGui::SetCursorPosX((window_size.x - btn_w) * 0.5f);
            if (ImGui::Button("Play", ImVec2(btn_w, 40))) {
                // Generate a unique device ID based on computer name + random
                char compName[256];
                DWORD compNameLen = sizeof(compName);
                GetComputerNameA(compName, &compNameLen);
                std::string deviceId = std::string(compName) + "-" + std::to_string(GetTickCount());
                
                nakamaClient.authenticateDevice(deviceId,
                    [&nakamaSession, &nakamaAuthPending](NakamaSession session) {
                        std::cout << "Successfully connected! Session token: " << session.token << std::endl;
                        nakamaSession = session;
                        nakamaAuthPending = true; // Signal main thread to switch state
                    },
                    [](std::string error) {
                        std::cout << "Connection failed! Error: " << error << std::endl;
                    }
                );
            }
            
            ImGui::Spacing();
            
            ImGui::SetCursorPosX((window_size.x - btn_w) * 0.5f);
            if (ImGui::Button("Quit", ImVec2(btn_w, 40))) {
                glfwSetWindowShouldClose(window, true);
            }
            
            ImGui::End();
        }

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

    nakamaClient.leaveMatch();
    ma_engine_uninit(&engine);

    glfwTerminate();
    return 0;
}