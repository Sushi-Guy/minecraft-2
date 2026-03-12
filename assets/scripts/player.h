#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "../../dependencies/miniaudio.h"
#include "object_loader.h"
bool isColliding(float px, float py, float pz, float radius, float height, Block b);

// Raycasts from the player's camera to find the first block hit.
// Returns the index of the block in the array, or -1 if nothing is hit.
// placeX, placeY, placeZ will contain the position just *before* the hit (where to place a block).
int raycast(float startX, float startY, float startZ, float pitch, float yaw, 
            Block level[], int numBlocks, float maxDist, float stepSize,
            float& placeX, float& placeY, float& placeZ) {
            
    // In OpenGL player_movement_update, we did:
    // forwardX = -sin(yawRad)
    // forwardZ = -cos(yawRad)
    // Let's make the raycast exactly match the camera's actual forward vector:
    float pitchRad = pitch * (3.14159265f / 180.0f);
    float yawRad = yaw * (3.14159265f / 180.0f);

    float dirX = -sin(yawRad) * cos(pitchRad);
    float dirY = sin(pitchRad); 
    float dirZ = -cos(yawRad) * cos(pitchRad);

    float currentX = startX;
    float currentY = startY;
    float currentZ = startZ;

    float prevX = startX;
    float prevY = startY;
    float prevZ = startZ;

    for (float dist = 0.0f; dist < maxDist; dist += stepSize) {
        prevX = currentX;
        prevY = currentY;
        prevZ = currentZ;

        currentX = startX + dirX * dist;
        currentY = startY + dirY * dist;
        currentZ = startZ + dirZ * dist;

        for (int i = 0; i < numBlocks; i++) {
            if (!level[i].isActive) continue;

            if (currentX >= level[i].minX && currentX <= level[i].maxX &&
                currentY >= level[i].minY && currentY <= level[i].maxY &&
                currentZ >= level[i].minZ && currentZ <= level[i].maxZ) {
                
                placeX = prevX;
                placeY = prevY;
                placeZ = prevZ;
                return i;
            }
        }
    }
    return -1;
}

void player_movement_update(GLFWwindow* window, float& playerX, float& playerY, float& playerZ, 
                            float& playerVelocityY, float& playerYaw, float& playerPitch, 
                            float& speed, float gravity, float jumpVelocity, 
                            float playerHeight, float playerRadius, bool& isOnGround,
                            float deltaTime, double& lastMouseX, double& lastMouseY, 
                            float mouseSensitivity, Block level[], int numBlocks, ma_engine* engine) {

    /* Poll for and process events */
    glfwPollEvents();

    // --- MOUSE LOOK INPUT ---
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    
    // Calculate how far the mouse moved since the last frame
    float xOffset = mouseX - lastMouseX;
    float yOffset = mouseY - lastMouseY;
    
    // Update our last known position to the current one
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    // Apply sensitivity to the raw mouse movement
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;
    
    // Add it to our camera angle!
    playerYaw += -xOffset;
    playerPitch += -yOffset; // Screen Y goes down, so this might be inverted depending on preference!
    
    // Constrain the pitch so you can't snap your neck by looking too far up or down
    if (playerPitch > 89.0f) playerPitch = 89.0f;
    if (playerPitch < -89.0f) playerPitch = -89.0f;

    // --- 3D KEYBOARD INPUT (Directional) ---
    // Convert the camera's yaw (degrees) into radians for math
    float yawRad = playerYaw * (3.14159265f / 180.0f);
    
    // In OpenGL:
    // -Z is forward, +X is right
    // When Yaw is 0, we want to move exactly along -Z
    // sin(0)=0, cos(0)=1
    float forwardX = -sin(yawRad);
    float forwardZ = -cos(yawRad);
    
    // Right vector is 90 degrees offset from forward
    float rightX = cos(yawRad);
    float rightZ = -sin(yawRad);

    if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
        speed = 3.5f;
    } else {
        speed = 2.0f;
    }

    // Convert our per-second speed into "how much to move this exact frame"
    float frameSpeed = speed * deltaTime; 

    // Move along the calculated vectors using W/A/S/D
    float moveX = 0.0f;
    float moveZ = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        moveX += forwardX * frameSpeed;
        moveZ += forwardZ * frameSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        moveX -= forwardX * frameSpeed;
        moveZ -= forwardZ * frameSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        moveX -= rightX * frameSpeed;
        moveZ -= rightZ * frameSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        moveX += rightX * frameSpeed;
        moveZ += rightZ * frameSpeed;
    }

    // Apply X movement and check collision
    float oldX = playerX;
    playerX += moveX;
    for (int i = 0; i < numBlocks; i++) {
        if (isColliding(playerX, playerY, playerZ, playerRadius, playerHeight, level[i])) {
            playerX = oldX; // Revert X
            break;
        }
    }

    // Apply Z movement and check collision
    float oldZ = playerZ;
    playerZ += moveZ;
    for (int i = 0; i < numBlocks; i++) {
        if (isColliding(playerX, playerY, playerZ, playerRadius, playerHeight, level[i])) {
            playerZ = oldZ; // Revert Z
            break;
        }
    }

    // Jump if on ground
    if (isOnGround && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        playerVelocityY = jumpVelocity;
        isOnGround = false;
    }

    // Apply Y movement (Gravity)
    playerVelocityY -= gravity * deltaTime;
    float oldY = playerY;
    playerY += playerVelocityY * deltaTime;

    isOnGround = false;
    for (int i = 0; i < numBlocks; i++) {
        if (isColliding(playerX, playerY, playerZ, playerRadius, playerHeight, level[i])) {
            if (playerVelocityY < 0.0f) { // Falling down (landed)
                playerY = level[i].maxY + playerHeight;
                isOnGround = true;
            } else if (playerVelocityY > 0.0f) { // Moving up (hit head)
                playerY = level[i].minY - 0.21f; // Slightly below head bounds
            }
            playerVelocityY = 0.0f;
            break;
        }
    }

    if(playerY < -50) {
        glfwSetWindowShouldClose(window, true);
    }

    static float footstepTimer = 0.0f;
    bool isMoving = (moveX != 0.0f || moveZ != 0.0f);
    
    if (isOnGround && isMoving) {
        footstepTimer -= deltaTime;
        if (footstepTimer <= 0.0f) {
            ma_engine_play_sound(engine, "assets/sounds/footstep.wav", NULL);
            footstepTimer = (speed > 2.5f) ? 0.35f : 0.5f;
        }
    } else if (!isMoving) {
        footstepTimer = 0.0f; 
    }
}