#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>
#include <functional>
#include "rendering.h"
#include "player.h"
#include "../../dependencies/miniaudio.h"

inline void handleMouseInteractions(GLFWwindow* window, float currentFrameTime, float deltaTime, 
    float& lastBreakTime, float breakCooldown,
    int& currentBreakingBlockIndex, float& currentBreakTime, float& lastBreakSoundTime, 
    float& lastPlaceTime, float placeCooldown,
    float playerX, float playerY, float playerZ, float playerPitch, float playerYaw, float playerRadius, float playerHeight,
    std::vector<Block>& level, int& NUM_BLOCKS, 
    std::vector<RenderChunk>& chunks, int CHUNK_SIZE,
    GLuint grassTex, GLuint dirtTex, GLuint stoneTex, GLuint oak_planksTex, GLuint currentSelectedTexture,
    ma_engine* engine, int blockSize, bool& leftMousePressed, bool& rightMousePressed,
    std::function<void(int)> onBlockBreak = nullptr,
    std::function<void(float,float,float,float,float,float,int)> onBlockPlace = nullptr,
    std::function<bool(const Block&)> isPlacementBlocked = nullptr) {
    
    // --- MOUSE CLICK INTERACTION ---
    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (leftDown) {
        float placeX, placeY, placeZ;
        int hitIndex = raycast(playerX, playerY, playerZ, playerPitch, playerYaw, level.data(), NUM_BLOCKS, 5.0f, 0.05f, placeX, placeY, placeZ);
        
        if (hitIndex != -1) {
            if (hitIndex != currentBreakingBlockIndex) {
                currentBreakingBlockIndex = hitIndex;
                currentBreakTime = 0.0f;
            }
            
            currentBreakTime += deltaTime;
            
            if (currentFrameTime - lastBreakSoundTime >= 0.25f) {
                GLuint hitTexture = level[hitIndex].textureID;
                if (hitTexture == grassTex || hitTexture == dirtTex) {
                    ma_engine_play_sound(engine, "assets/sounds/dirt_break.wav", NULL);
                } else if (hitTexture == stoneTex) {
                    ma_engine_play_sound(engine, "assets/sounds/stone_break.wav", NULL);
                } else if (hitTexture == oak_planksTex) {
                    ma_engine_play_sound(engine, "assets/sounds/wood_break.wav", NULL);
                } else {
                    ma_engine_play_sound(engine, "assets/sounds/default_break.wav", NULL);
                }
                lastBreakSoundTime = currentFrameTime;
            }

            float requiredBreakTime = 0.5f;
            GLuint tex = level[hitIndex].textureID;
            if (tex == grassTex || tex == dirtTex) {
                requiredBreakTime = 0.5f;
            } else if (tex == oak_planksTex) {
                requiredBreakTime = 1.0f;
            } else if (tex == stoneTex) {
                requiredBreakTime = 1.5f;
            }

            if (currentBreakTime >= requiredBreakTime) {
                level[hitIndex].isActive = false;
                chunks[hitIndex / CHUNK_SIZE].dirty = true;
                
                currentBreakTime = 0.0f;
                currentBreakingBlockIndex = -1;
                lastBreakTime = currentFrameTime;

                // Notify multiplayer
                if (onBlockBreak) onBlockBreak(hitIndex);
            }
        } else {
            currentBreakingBlockIndex = -1;
            currentBreakTime = 0.0f;
        }
    } else {
        currentBreakingBlockIndex = -1;
        currentBreakTime = 0.0f;
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
                gridX, gridX + (float)blockSize,
                gridY, gridY + 1.0f, // Make placed blocks 1 unit tall
                gridZ, gridZ + (float)blockSize,
                currentSelectedTexture,
                true
            };
            
            // Calculate distance from player's eyes to the block placement position
            float dx = placeX - playerX;
            float dy = placeY - playerY;
            float dz = placeZ - playerZ;
            float distToPlace = sqrt(dx*dx + dy*dy + dz*dz);

            // Enforce a minimum distance (e.g. 1.0 blocks) and check strict collision 
            const bool blockedByEntity = isPlacementBlocked ? isPlacementBlocked(newBlock) : false;

            if (distToPlace >= 1.0f && !isColliding(playerX, playerY, playerZ, playerRadius, playerHeight, newBlock) && !blockedByEntity) {
                // Try to find an inactive block to replace
                int placedIndex = -1;
                for (int i = 0; i < NUM_BLOCKS; i++) {
                    if (!level[i].isActive) {
                        level[i] = newBlock;
                        placedIndex = i;
                        break;
                    }
                }
                
                if (placedIndex == -1) {
                    // Append to the level array if no inactive blocks found
                    level.push_back(newBlock);
                    placedIndex = NUM_BLOCKS;
                    NUM_BLOCKS++;
                    
                    // See if we need to add to the last chunk, or make a new one
                    if (chunks.empty()) {
                        RenderChunk newChunk;
                        newChunk.listId = glGenLists(1);
                        newChunk.dirty = true;
                        newChunk.startIdx = 0;
                        newChunk.endIdx = 1;
                        chunks.push_back(newChunk);
                    } else {
                        RenderChunk& last = chunks.back();
                        if (last.endIdx - last.startIdx < CHUNK_SIZE) {
                            last.endIdx++;
                        } else {
                            RenderChunk newChunk;
                            newChunk.listId = glGenLists(1);
                            newChunk.dirty = true;
                            newChunk.startIdx = placedIndex;
                            newChunk.endIdx = placedIndex + 1;
                            chunks.push_back(newChunk);
                        }
                    }
                }
                
                chunks[placedIndex / CHUNK_SIZE].dirty = true;
                lastPlaceTime = currentFrameTime;

                // Notify multiplayer
                if (onBlockPlace) onBlockPlace(
                    newBlock.minX, newBlock.maxX,
                    newBlock.minY, newBlock.maxY,
                    newBlock.minZ, newBlock.maxZ,
                    (int)currentSelectedTexture);

                // Play sound
                if(currentSelectedTexture == dirtTex || currentSelectedTexture == grassTex) {
                    // Play dirt place sound
                    ma_engine_play_sound(engine, "assets/sounds/dirt_place.wav", NULL);
                } else if(currentSelectedTexture == stoneTex) {
                    // Play stone place sound
                    ma_engine_play_sound(engine, "assets/sounds/stone_place.wav", NULL);
                } else if(currentSelectedTexture == oak_planksTex) {
                    ma_engine_play_sound(engine, "assets/sounds/wood_place.wav", NULL);
                }
            }
        }
    }
    rightMousePressed = rightDown;
}
