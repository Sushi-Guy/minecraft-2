#pragma once
#include <GL/glew.h>

struct Block {
    float minX, maxX, minY, maxY, minZ, maxZ;
    GLuint textureID;
    bool isActive = true; // For chunk optimization
};

struct RenderChunk {
    GLuint listId;
    bool dirty;
    int startIdx;
    int endIdx;
};

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include "stb_image.h"

inline GLuint loadTexture(const char* filepath) {
    int width, height, nrChannels;
    unsigned char *data = stbi_load(filepath, &width, &height, &nrChannels, 0);
    if (!data) {
        std::cout << "Failed to load texture: " << filepath << std::endl;
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

inline void getBlockPoints(Block b, float points[6][4][3]) {
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

struct Material {
    std::string name;
    float r = 1.0f, g = 1.0f, b = 0.0f; // Default to purple
};

struct ModelVertex {
    float x, y, z;
};

struct ModelNormal {
    float x, y, z;
};

struct ModelFace {
    int v1, v2, v3; // Indices of 3 vertices to make a triangle
    int n1 = -1, n2 = -1, n3 = -1; // Indices of 3 normals
    int materialIndex = -1; // -1 means no material
};

struct Model {
    std::vector<ModelVertex> vertices;
    std::vector<ModelNormal> normals;
    std::vector<ModelFace> faces;
    std::vector<Material> materials;
};

void parseMTL(const std::string& filepath, Model& model) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "Failed to open MTL file: " << filepath << std::endl;
        return;
    }

    std::string line;
    Material currentMaterial;
    bool hasMaterial = false;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "newmtl") {
            if (hasMaterial) {
                model.materials.push_back(currentMaterial);
            }
            iss >> currentMaterial.name;
            currentMaterial.r = 1.0f; currentMaterial.g = 1.0f; currentMaterial.b = 1.0f;
            hasMaterial = true;
        } else if (type == "Kd" && hasMaterial) {
            iss >> currentMaterial.r >> currentMaterial.g >> currentMaterial.b;
        }
    }
    
    if (hasMaterial) {
        model.materials.push_back(currentMaterial);
    }
}

Model loadOBJ(const char* filepath) {
    Model model;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "Failed to open OBJ file: " << filepath << std::endl;
        return model;
    }

    // Get directory path for MTL loading
    std::string dirPath = "";
    std::string pathStr(filepath);
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        dirPath = pathStr.substr(0, lastSlash + 1);
    }

    std::string line;
    int currentMaterialIndex = -1;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "mtllib") {
            std::string mtlFile;
            iss >> mtlFile;
            parseMTL(dirPath + mtlFile, model);
        } else if (type == "usemtl") {
            std::string matName;
            iss >> matName;
            currentMaterialIndex = -1; // reset
            for (size_t i = 0; i < model.materials.size(); ++i) {
                if (model.materials[i].name == matName) {
                    currentMaterialIndex = i;
                    break;
                }
            }
        } else if (type == "v") {
            ModelVertex vertex;
            iss >> vertex.x >> vertex.y >> vertex.z;
            model.vertices.push_back(vertex);
        } else if (type == "vn") {
            ModelNormal normal;
            iss >> normal.x >> normal.y >> normal.z;
            model.normals.push_back(normal);
        } else if (type == "f") {
            std::vector<int> current_face;
            std::vector<int> current_normals;
            std::string v_str;
            while (iss >> v_str) {
                // Extract just the first integer before any slashes
                size_t first_slash = v_str.find('/');
                if (first_slash != std::string::npos) {
                    current_face.push_back(std::stoi(v_str.substr(0, first_slash)) - 1);
                    size_t second_slash = v_str.find('/', first_slash + 1);
                    if (second_slash != std::string::npos && second_slash + 1 < v_str.length()) {
                        current_normals.push_back(std::stoi(v_str.substr(second_slash + 1)) - 1);
                    } else {
                        current_normals.push_back(-1);
                    }
                } else {
                    current_face.push_back(std::stoi(v_str) - 1); // OBJ indices are 1-based
                    current_normals.push_back(-1);
                }
            }
            // Triangulate the polygon (fan triangulation)
            for (size_t i = 1; i + 1 < current_face.size(); ++i) {
                ModelFace face;
                face.v1 = current_face[0];
                face.v2 = current_face[i];
                face.v3 = current_face[i + 1];
                face.n1 = current_normals[0];
                face.n2 = current_normals[i];
                face.n3 = current_normals[i + 1];
                face.materialIndex = currentMaterialIndex;
                model.faces.push_back(face);
            }
        }
    }

    return model;
}

void draw_model(const Model& model, float defaultColor[4]) {
    glBegin(GL_TRIANGLES);
    for (const auto& face : model.faces) {
        if (face.materialIndex >= 0 && face.materialIndex < model.materials.size()) {
            const Material& mat = model.materials[face.materialIndex];
            glColor3f(mat.r, mat.g, mat.b);
        } else {
            glColor4fv(defaultColor);
        }

        if (face.v1 < model.vertices.size() && face.v2 < model.vertices.size() && face.v3 < model.vertices.size()) {
            if (face.n1 >= 0 && face.n1 < model.normals.size()) glNormal3f(model.normals[face.n1].x, model.normals[face.n1].y, model.normals[face.n1].z);
            glVertex3f(model.vertices[face.v1].x, model.vertices[face.v1].y, model.vertices[face.v1].z);

            if (face.n2 >= 0 && face.n2 < model.normals.size()) glNormal3f(model.normals[face.n2].x, model.normals[face.n2].y, model.normals[face.n2].z);
            glVertex3f(model.vertices[face.v2].x, model.vertices[face.v2].y, model.vertices[face.v2].z);

            if (face.n3 >= 0 && face.n3 < model.normals.size()) glNormal3f(model.normals[face.n3].x, model.normals[face.n3].y, model.normals[face.n3].z);
            glVertex3f(model.vertices[face.v3].x, model.vertices[face.v3].y, model.vertices[face.v3].z);
        }
    }
    glEnd();
}



void draw_triangle_color(float color[4], float points[3][3], float thickness, float outlineColor[4], bool activated) {
    glBegin(GL_TRIANGLES);
    glColor4fv(color);
    glVertex3fv(points[0]);
    glVertex3fv(points[1]);
    glVertex3fv(points[2]);
    glEnd();

    // Draw the outline if activated
    if (activated) {
        glLineWidth(thickness);
        glBegin(GL_LINE_LOOP);
        glColor4fv(outlineColor);
        glVertex3fv(points[0]);
        glVertex3fv(points[1]);
        glVertex3fv(points[2]);
        glEnd();
        glLineWidth(1.0f); // Reset thickness
    }
}

void draw_square_color(float color[4], float points[4][3], float thickness, float outlineColor[4], bool activated) {
    glBegin(GL_QUADS);
    glColor4fv(color);
    glVertex3fv(points[0]); // Top-Left
    glVertex3fv(points[1]); // Top-Right
    glVertex3fv(points[2]); // Bottom-Right
    glVertex3fv(points[3]); // Bottom-Left
    glEnd();

    // Draw the outline if activated
    if (activated) {
        glLineWidth(thickness);
        glBegin(GL_LINE_LOOP);
        glColor4fv(outlineColor);
        glVertex3fv(points[0]); 
        glVertex3fv(points[1]); 
        glVertex3fv(points[2]); 
        glVertex3fv(points[3]); 
        glEnd();
        glLineWidth(1.0f); // Reset thickness
    }
}

void draw_cube_texture(GLuint textureID, float points[6][4][3], float outlineColor[4], float thickness, bool activated, bool tiled = false, const float* blockColor = nullptr) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureID);
    if (blockColor) {
        glColor4fv(blockColor);
    } else {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Make sure color doesn't tint the texture
    }

    glBegin(GL_QUADS);
    for(int i = 0; i < 6; i++) {
        if (i == 0) glNormal3f(0.0f, 0.0f, 1.0f); // Front
        else if (i == 1) glNormal3f(0.0f, 0.0f, -1.0f); // Back
        else if (i == 2) glNormal3f(0.0f, 1.0f, 0.0f); // Top
        else if (i == 3) glNormal3f(0.0f, -1.0f, 0.0f); // Bottom
        else if (i == 4) glNormal3f(-1.0f, 0.0f, 0.0f); // Left
        else if (i == 5) glNormal3f(1.0f, 0.0f, 0.0f); // Right

        float u_scale = 1.0f;
        float v_scale = 1.0f;

        if (tiled) {
            // Calculate the width (u_scale) and height (v_scale) of this face
            float dxU = points[i][1][0] - points[i][0][0];
            float dyU = points[i][1][1] - points[i][0][1];
            float dzU = points[i][1][2] - points[i][0][2];
            u_scale = sqrt(dxU*dxU + dyU*dyU + dzU*dzU);

            float dxV = points[i][0][0] - points[i][3][0];
            float dyV = points[i][0][1] - points[i][3][1];
            float dzV = points[i][0][2] - points[i][3][2];
            v_scale = sqrt(dxV*dxV + dyV*dyV + dzV*dzV);
        }

        // Based on CCW Winding: 0=BottomLeft, 1=BottomRight, 2=TopRight, 3=TopLeft
        glTexCoord2f(0.0f, 0.0f);       glVertex3fv(points[i][0]);
        glTexCoord2f(u_scale, 0.0f);    glVertex3fv(points[i][1]);
        glTexCoord2f(u_scale, v_scale); glVertex3fv(points[i][2]);
        glTexCoord2f(0.0f, v_scale);    glVertex3fv(points[i][3]);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);

    if (activated) {
        glLineWidth(thickness);
        for(int i = 0; i < 6; i++) {
            glBegin(GL_LINE_LOOP);
            glColor4fv(outlineColor);
            glVertex3fv(points[i][0]); 
            glVertex3fv(points[i][1]); 
            glVertex3fv(points[i][2]); 
            glVertex3fv(points[i][3]); 
            glEnd();
        }
        glLineWidth(1.0f);
    }
}