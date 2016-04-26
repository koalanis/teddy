#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/string_cast.hpp>

// OpenGL library includes
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <SDL.h> 
#include <SDL_image.h>

#define DEBUG 0



int window_width = 800, window_height = 600;
const std::string window_title = "OBJ Loader";

// VBO and VAO descriptors.
enum { 
	kVertexBuffer, // Buffer of vertex positions
	kIndexBuffer,  // Buffer of triangle indices
	kNumVbos };

GLuint vao = 0;                   // This will store the VAO descriptor.
GLuint buffer_objects[kNumVbos];  // These will store VBO descriptors.

const char* vertex_shader =
    "#version 330 core\n"
    "in vec3 vertex_position;" // A vector (x,y,z) representing the vertex's position
    "uniform vec3 light_position;" // Global variable representing the light's position
    "out vec3 vs_light_direction;" // Used for shading by the fragment shader
    "void main() {"
       "gl_Position = vec4(vertex_position, 1.0);" // Don't transform the vertices at all
       "vs_light_direction = light_position - vertex_position;" // Calculate vector to the light (used for shading in fragment shader)
    "}";

const char* geometry_shader =
    "#version 330 core\n"
    "layout (triangles) in;" // Reads in triangles
    "layout (triangle_strip, max_vertices = 3) out;" // And outputs triangles
    "uniform mat4 view_projection;" // The matrix encoding the camera position and settings. Don't worry about this for now
    "in vec3 vs_light_direction[];" // The light direction computed in the vertex shader
    "out vec3 normal;" // The normal of the triangle. Needs to be computed inside this shader
    "out vec3 light_direction;" // Light direction again (this is just passed straight through to the fragment shader)
    "void main() {"
    	//Took the cross product of (p2 - p1) and (p3 - p1) which returns a vector
       "vec3 norm = cross( gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz, gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz);"
       // normalized the resulting vector to obtain the unit vector
       "normal = normalize(norm);"
       "int n = 0;"
       "for (n = 0; n < gl_in.length(); n++) {" // Loop over three vertices of the triangle
          "light_direction = vs_light_direction[n];" // Pass the light direction to the fragment shader
          "gl_Position = view_projection * gl_in[n].gl_Position;" // Project the vertex into screen coordinates
          "EmitVertex();"
       "}"
       "EndPrimitive();"
    "}";

const char* fragment_shader =
    "#version 330 core\n"
    "in vec3 normal;" // Normal computed in the geometry shader
    "in vec3 light_direction;" // Light direction computed in the vertex shader
    "out vec4 fragment_color;" // This shader will compute the pixel color
    "void main() {"
       "vec4 color = vec4(1.0, 0.0, 0.0, 1.0);" // Red
       "float dot_nl = dot(normalize(light_direction), normalize(normal));" // Compute brightness based on angle between normal and light
       "dot_nl = clamp(dot_nl, 0.0, 1.0);" // Ignore back-facing triangles
       "fragment_color = clamp(dot_nl * color, 0.0, 1.0);"
    "}";
//---------------------------------------------

const char* sprite_vertex_shader =
  "#version 330 core"
  "layout (location = 0) in vec4 vertex;" // <vec2 position, vec2 texCoords>
  "out vec2 TexCoords;"
  "uniform mat4 model;"
  "uniform mat4 projection;"
  "void main()"
  "{"
  "    TexCoords = vertex.zw;"
  "    gl_Position = projection * model * vec4(vertex.xy, 0.0, 1.0);"
  "}"

const char* sprite_fragment_shader =
  "#version 330 core"
  "in vec2 TexCoords;"
  "out vec4 color;"
  "uniform sampler2D image;"
  "uniform vec3 spriteColor;"
  "void main()"
  "{"
  "    color = vec4(spriteColor, 1.0)*texture(image, TexCoords);"
  "}"

// Functions and macros to help debug GL errors

const char* OpenGlErrorToString(GLenum error) {
  switch (error) {
    case GL_NO_ERROR:
      return "GL_NO_ERROR";
      break;
    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";
      break;
    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";
      break;
    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";
      break;
    case GL_OUT_OF_MEMORY:
      return "GL_OUT_OF_MEMORY";
      break;
    default:
      return "Unknown Error";
      break;
  }
  return "Unicorns Exist";
}

#define CHECK_SUCCESS(x) \
  if (!(x)) {            \
    glfwTerminate();     \
    exit(EXIT_FAILURE);  \
  }

#define CHECK_GL_SHADER_ERROR(id)                                           \
  {                                                                          \
    GLint status = 0;                                                       \
    GLint length = 0;                                                       \
    glGetShaderiv(id, GL_COMPILE_STATUS, &status);                          \
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);                         \
    if (!status) {                                                          \
      std::string log(length, 0);                                           \
      glGetShaderInfoLog(id, length, nullptr, &log[0]);                     \
      std::cerr << "Line :" << __LINE__ << " OpenGL Shader Error: Log = \n" \
                << &log[0];                                                 \
      glfwTerminate();                                                      \
      exit(EXIT_FAILURE);                                                   \
    }                                                                       \
  }

#define CHECK_GL_PROGRAM_ERROR(id)                                           \
  {                                                                          \
    GLint status = 0;                                                        \
    GLint length = 0;                                                        \
    glGetProgramiv(id, GL_LINK_STATUS, &status);                             \
    glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);                         \
    if (!status) {                                                           \
      std::string log(length, 0);                                            \
      glGetProgramInfoLog(id, length, nullptr, &log[0]);                     \
      std::cerr << "Line :" << __LINE__ << " OpenGL Program Error: Log = \n" \
                << &log[0];                                                  \
      glfwTerminate();                                                       \
      exit(EXIT_FAILURE);                                                    \
    }                                                                        \
  }

#define CHECK_GL_ERROR(statement)                                             \
  {                                                                           \
    { statement; }                                                            \
    GLenum error = GL_NO_ERROR;                                               \
    if ((error = glGetError()) != GL_NO_ERROR) {                              \
      std::cerr << "Line :" << __LINE__ << " OpenGL Error: code  = " << error \
                << " description =  " << OpenGlErrorToString(error);          \
      glfwTerminate();                                                        \
      exit(EXIT_FAILURE);                                                     \
    }                                                                         \
  }


void LoadObj(const std::string& file, std::vector<glm::vec3>& vertices,
             std::vector<glm::uvec3>& indices) 
{
 
  std::cout << "LOADOBJ INVOKED" << std::endl;
  std::ifstream myfile (file);
  std::string line;
  //Open the obj file containing indicies and vertices
  if(myfile.is_open())
  {
    while (getline(myfile, line))
    {
      // read the file
      std::istringstream is (line);
      char flag;
      is >> flag;
      switch(flag)
      {
      	//in the case that the line begins with v then add the point to the vertices vector
        case 'v':
        {
          float v1,v2,v3;
          is >> v1;
          is >> v2;
          is >> v3;
          vertices.push_back(glm::vec3(v1,v2,v3));
        }break;
        case 'f':
        {
          //in the case that the line begins with f then add the 3 points to the indices vector
          int v1,v2,v3;
          is >> v1;
          is >> v2;
          is >> v3;

          // Zero indexing because obj indices were 1 indexing
          indices.push_back(glm::uvec3(v1-1,v2-1,v3-1));
        
        
        }break;
        default:
        break;
      }
    }
    myfile.close();
  }
  else {
    std::cout << "Unable to open file" << std::endl;
  } 
}


void ErrorCallback(int error, const char* description) {
  std::cerr << "GLFW Error: " << description << "\n";
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GL_TRUE);
}

class SpriteRenderer
{
public:
  SpriteRenderer(Shader);
  ~SpriteRenderer();

  /* data */
};



int main(int argc, char* argv[]) {
  std::string file;
  if(argc > 1)
  {
     file = std::string(argv[1]);
     std::cout << "file = " << file << "\n";
  }

  // Set up OpenGL context
  if (!glfwInit()) 
    exit(EXIT_FAILURE);
  
  glfwSetErrorCallback(ErrorCallback);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow* window = glfwCreateWindow(window_width, window_height,
                                        &window_title[0], nullptr, nullptr);
  CHECK_SUCCESS(window != nullptr);
  glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  CHECK_SUCCESS(glewInit() == GLEW_OK);
  glGetError();  // clear GLEW's error for it
  glfwSetKeyCallback(window, KeyCallback);
  glfwSwapInterval(1);
  //-------------------------------------------------------------------------
  const GLubyte* renderer = glGetString(GL_RENDERER);  // get renderer string
  const GLubyte* version = glGetString(GL_VERSION);    // version as a string
  const GLubyte* glsl_version =
      glGetString(GL_SHADING_LANGUAGE_VERSION);  // version as a
                                                 // string
  std::cout << "Renderer: " << renderer << "\n";
  std::cout << "OpenGL version supported:" << version << "\n";
  std::cout << "GLSL version supported:" << glsl_version << "\n";
  //-------------------------------------------------------------------------
  

  // Load geometry to render
  std::vector<glm::vec3> obj_vertices;
  std::vector<glm::uvec3> obj_faces; 
  LoadObj(file, obj_vertices, obj_faces);
  std::cout << "Found " << obj_vertices.size() << " vertices and "
            << obj_faces.size() << " faces.\n";

  // Create Vertex Array Object
  CHECK_GL_ERROR(glGenVertexArrays(1, &vao));
  CHECK_GL_ERROR(glBindVertexArray(vao));

  // Create Vertex Buffer Objects
  CHECK_GL_ERROR(glGenBuffers(2, buffer_objects));

  // Vertex positions
  CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, buffer_objects[kVertexBuffer]));
  // NOTE: We do not send anything right now, we just describe it to OpenGL.
  CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
                              sizeof(float) * obj_vertices.size() * 3, // total size of the position buffer
			      nullptr, // don't provide data yet, we will pass it in during the rendering loop
                              GL_STATIC_DRAW));
  CHECK_GL_ERROR(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0)); // Assign position buffer to vertex attribute 0
  CHECK_GL_ERROR(glEnableVertexAttribArray(0)); 

  // Triangle indices
  CHECK_GL_ERROR(
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_objects[kIndexBuffer]));
  CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                              sizeof(uint32_t) * obj_faces.size() * 3, // total size of the triangle index buffer
                              &obj_faces[0], // pointer to the data to pass to the GPU
			      GL_STATIC_DRAW));


  // Create shader program
  GLuint program_id = 0;
  CHECK_GL_ERROR(program_id = glCreateProgram());
    
  // Compile shaders and attach to shader program
  // One vertex shader
  GLuint vertex_shader_id = 0;
  const char* vertex_source_pointer = vertex_shader;
  CHECK_GL_ERROR(vertex_shader_id = glCreateShader(GL_VERTEX_SHADER));
  CHECK_GL_ERROR(
      glShaderSource(vertex_shader_id, 1, &vertex_source_pointer, nullptr));
  glCompileShader(vertex_shader_id);
  CHECK_GL_SHADER_ERROR(vertex_shader_id);

  // one geometry shader
  GLuint geometry_shader_id = 0;
  const char* geometry_source_pointer = geometry_shader;
  CHECK_GL_ERROR(geometry_shader_id = glCreateShader(GL_GEOMETRY_SHADER));
  CHECK_GL_ERROR(
      glShaderSource(geometry_shader_id, 1, &geometry_source_pointer, nullptr));
  glCompileShader(geometry_shader_id);
  CHECK_GL_SHADER_ERROR(geometry_shader_id);

  // one fragment shader
  GLuint fragment_shader_id = 0;
  const char* fragment_source_pointer = fragment_shader;
  CHECK_GL_ERROR(fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER));
  CHECK_GL_ERROR(
      glShaderSource(fragment_shader_id, 1, &fragment_source_pointer, nullptr));
  glCompileShader(fragment_shader_id);
  CHECK_GL_SHADER_ERROR(fragment_shader_id);

  CHECK_GL_ERROR(glAttachShader(program_id, vertex_shader_id));
  CHECK_GL_ERROR(glAttachShader(program_id, fragment_shader_id));
  CHECK_GL_ERROR(glAttachShader(program_id, geometry_shader_id));

  // Link shader program
  CHECK_GL_ERROR(glBindAttribLocation(program_id, 0, "vertex_position"));
  CHECK_GL_ERROR(glBindFragDataLocation(program_id, 0, "fragment_color"));
  glLinkProgram(program_id);
  CHECK_GL_PROGRAM_ERROR(program_id);
  GLint view_projection_matrix_location = 0;
  CHECK_GL_ERROR(view_projection_matrix_location =
                     glGetUniformLocation(program_id, "view_projection"));
  GLint light_position_location = 0;
  CHECK_GL_ERROR(light_position_location =
                     glGetUniformLocation(program_id, "light_position"));

  // Set up camera and light (ignore for now)
  glm::vec3 min_bounds = glm::vec3(std::numeric_limits<float>::max());
  glm::vec3 max_bounds = glm::vec3(-std::numeric_limits<float>::max());
  for (int i = 0; i < obj_vertices.size(); ++i) {
    min_bounds = glm::min(obj_vertices[i], min_bounds);
    max_bounds = glm::max(obj_vertices[i], max_bounds);
  }
  std::cout << "min_bounds = " << glm::to_string(min_bounds) << "\n";
  std::cout << "max_bounds = " << glm::to_string(max_bounds) << "\n";
  std::cout << "center = " << glm::to_string(0.5f * (min_bounds + max_bounds))
            << "\n";
  



  glm::vec3 light_position = glm::vec3(10.0f, 0.0f, 10.0f);
  glm::vec3 eye = glm::vec3(0.0f, 0.1f, 0.4f);
  glm::vec3 look = glm::vec3(0.0f, 0.1f, 0.0f);
  glm::vec3 up = glm::vec3(0.0f, 0.1f, 0.4f);
  glm::mat4 view_matrix = glm::lookAt(eye, look, up);

  float aspect = static_cast<float>(window_width) / window_height;
  glm::mat4 projection_matrix = glm::perspective(glm::radians(45.0f), aspect, 0.0001f, 1000.0f);
  // glm::mat4 projection_matrix = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
  glm::mat4 view_projection_matrix = projection_matrix * view_matrix;

  while (!glfwWindowShouldClose(window)) 
  {
    // Clear screen
    glfwGetFramebufferSize(window, &window_width, &window_height);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LESS);

    // Tell OpenGL what shader program to use
    CHECK_GL_ERROR(glUseProgram(program_id));

    // Tell OpenGL what to render
    CHECK_GL_ERROR(
        glBindBuffer(GL_ARRAY_BUFFER, buffer_objects[kVertexBuffer]));
    CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER,
                                sizeof(float) * obj_vertices.size() * 3, // same size as before
                                &obj_vertices[0], // this time we do pass the vertex position data
				GL_STATIC_DRAW));

    // Pass in global variables
    CHECK_GL_ERROR(glUniformMatrix4fv(view_projection_matrix_location, 1,
                                      GL_FALSE, &view_projection_matrix[0][0]));
    CHECK_GL_ERROR(
        glUniform3fv(light_position_location, 1, &light_position[0]));
    CHECK_GL_ERROR(glBindVertexArray(vao));

    // Render!
    CHECK_GL_ERROR(
        glDrawElements(GL_TRIANGLES, obj_faces.size() * 3, GL_UNSIGNED_INT, 0));


    glfwPollEvents();
    glfwSwapBuffers(window);
  }
  glfwDestroyWindow(window);
  glfwTerminate();
  exit(EXIT_SUCCESS);
}