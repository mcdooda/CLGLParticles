#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cmath>
#include <CL/opencl.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>

#ifdef _WIN32
#include <windows.h>
#define getCurrentDeviceContext() wglGetCurrentDC()
#define DEVICE_CONTEXT_PROPERTY_NAME CL_WGL_HDC_KHR
#else
#error Unsupported platform
#endif

#define GL_SHARING_EXTENSION "cl_khr_gl_sharing"

// read shader or opencl file
std::string readFile(const std::string& filePath);

// load image as sdl surface and upload to gpu
GLuint loadImage(const std::string& filePath);

// shaders
GLuint compileProgram(GLuint vertexShaderId, GLuint geometryShaderId, GLuint fragmentShaderId);
bool checkProgram(GLuint programId);
GLuint loadShader(GLenum shaderType, const GLchar* source);
bool checkShader(GLuint shaderId);

// OpenCL
const char* getErrorString(cl_int error);
std::string getErrorLog(cl_program program, cl_device_id deviceId);

#define DEBUG_BREAK() *(int*)0 = 0

#define CHECK_ERROR_CODE(function)													\
	if (code != CL_SUCCESS)															\
	{																				\
		std::cerr << #function " returned " << code << ": " << getErrorString(code)	\
			<< " (line " << __LINE__ << ")" << std::endl;							\
		DEBUG_BREAK();																\
		return EXIT_FAILURE;														\
	}

#define CHECK_ERROR_CODE_LOG(function)												\
	if (code != CL_SUCCESS)															\
	{																				\
		std::cerr << #function " returned " << code << ": " << getErrorString(code)	\
			<< " (line " << __LINE__ << ")" << std::endl							\
			<< "Log:" << std::endl													\
			<< getErrorLog(program, deviceId) << std::endl;							\
		DEBUG_BREAK();																\
		return EXIT_FAILURE;														\
	}


int main(int argc, char* argv[])
{
	// init SDL window
	SDL_Init(SDL_INIT_VIDEO);

	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);
	unsigned int windowWidth = static_cast<unsigned int>(static_cast<float>(displayMode.w) * 0.75f);
	unsigned int windowHeight = static_cast<unsigned int>(static_cast<float>(displayMode.h) * 0.75f);

	srand(static_cast<unsigned int>(time(nullptr)));

	SDL_Window* window = SDL_CreateWindow(
		"OpenGL/OpenCL Test",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		windowWidth, windowHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
	);
	if (window == nullptr)
	{
		std::cerr << "Could not open SDL window" << std::endl;
		return EXIT_FAILURE;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	if (glContext == nullptr)
	{
		std::cerr << "Could not create GL context" << std::endl;
		return EXIT_FAILURE;
	}

	SDL_GL_MakeCurrent(window, glContext);

	// init OpenGL
	glewExperimental = GL_TRUE;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	int err = glewInit();
	if (err != GLEW_OK)
	{
		std::cerr << "glewInit failed: " << glewGetErrorString(err) << std::endl;
		return EXIT_FAILURE;
	}

	if (!(GLEW_ARB_vertex_program
		&& GLEW_ARB_fragment_program
		&& GLEW_ARB_texture_float
		&& GLEW_ARB_draw_buffers
		&& GLEW_ARB_framebuffer_object))
	{
		std::cerr << "Shaders not supported!" << std::endl;
		return EXIT_FAILURE;
	}

	std::string vertexShaderSource = readFile("shaders/shader.vert");
	GLuint vertexShaderId = loadShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
	if (vertexShaderId == 0)
	{
		DEBUG_BREAK();
		return EXIT_FAILURE;
	}

	std::string geometryShaderSource = readFile("shaders/shader.geom");
	GLuint geometryShaderId = loadShader(GL_GEOMETRY_SHADER, geometryShaderSource.c_str());
	if (geometryShaderId == 0)
	{
		DEBUG_BREAK();
		return EXIT_FAILURE;
	}

	std::string fragmentShaderSource = readFile("shaders/shader.frag");
	GLuint fragmentShaderId = loadShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
	if (fragmentShaderId == 0)
	{
		DEBUG_BREAK();
		return EXIT_FAILURE;
	}

	GLuint programId = compileProgram(vertexShaderId, geometryShaderId, fragmentShaderId);
	if (programId == 0)
	{
		DEBUG_BREAK();
		return EXIT_FAILURE;
	}

	GLint particleTextureUniform = glGetUniformLocation(programId, "particleTexture");
	if (particleTextureUniform == -1)
		std::cerr << "warning: particleTextureUniform invalid" << std::endl;

	GLint projectionMatrixUniform = glGetUniformLocation(programId, "projectionMatrix");
	if (projectionMatrixUniform == -1)
		std::cerr << "warning: projectionMatrixUniform invalid" << std::endl;

	GLint modelViewMatrixUniform = glGetUniformLocation(programId, "modelViewMatrix");
	if (modelViewMatrixUniform == -1)
		std::cerr << "warning: modelViewMatrixUniform invalid" << std::endl;

	GLint positionAttribute = glGetAttribLocation(programId, "position");
	if (positionAttribute == -1)
		std::cerr << "warning: positionAttribute invalid" << std::endl;

	GLint isAliveAttribute = glGetAttribLocation(programId, "isAlive");
	if (isAliveAttribute == -1)
		std::cerr << "warning: isAliveAttribute invalid" << std::endl;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.f, 0.f, 0.f, 1.f);

	glm::mat4 projectionMatrix;
	auto updateWindowSize = [&windowWidth, &windowHeight, &projectionMatrix](unsigned int width, unsigned int height)
	{
		windowWidth = width;
		windowHeight = height;

		// viewport
		glViewport(0, 0, windowWidth, windowHeight);

		// projection
		projectionMatrix = glm::perspectiveFov(
			glm::radians(75.f),
			static_cast<float>(windowWidth), static_cast<float>(windowHeight),
			0.1f, 1000.f
		);
	};
	updateWindowSize(windowWidth, windowHeight);

	// view matrix
	cl_float3 cameraPosition{ 0.f, 20.f, -23.f };
	float cameraElevation = -glm::pi<float>() * 0.25f;

	cl_float3 cameraForward;
	const float cameraSpeed = 50.f;
	const float cameraRotationSpeed = glm::radians(45.f);

	glm::mat4 modelViewMatrix;
	auto updateCamera = [&modelViewMatrix, &cameraPosition, &cameraForward, &cameraElevation]()
	{
		cameraForward.s[0] = 0.f;
		cameraForward.s[1] = std::sin(cameraElevation);
		cameraForward.s[2] = std::cos(cameraElevation);
		modelViewMatrix = glm::mat4(1.f);
		modelViewMatrix = glm::translate(
			modelViewMatrix,
			glm::vec3(-cameraPosition.s[0], -cameraPosition.s[1], -cameraPosition.s[2])
		);
		modelViewMatrix = glm::lookAt(
			glm::vec3(cameraPosition.s[0], cameraPosition.s[1], cameraPosition.s[2]),
			glm::vec3(cameraPosition.s[0], cameraPosition.s[1], cameraPosition.s[2]) + glm::vec3(cameraForward.s[0], cameraForward.s[1], cameraForward.s[2]),
			glm::vec3(0.f, 1.f, 0.f)
		);
	};
	updateCamera();

	// load particle texture
	GLuint textureId = loadImage("data/particle.png");

	// init OpenCL
	cl_int code;

	// platform
	cl_platform_id platformId;
	code = clGetPlatformIDs(1, &platformId, nullptr);
	CHECK_ERROR_CODE(clGetPlatformIDs);

	// device
	cl_device_id deviceId;
	code = clGetDeviceIDs(platformId, CL_DEVICE_TYPE_GPU, 1, &deviceId, nullptr);
	CHECK_ERROR_CODE(clGetDeviceIDs);

	char deviceString[1024];
	clGetDeviceInfo(deviceId, CL_DEVICE_NAME, sizeof(deviceString), &deviceString, NULL);
	std::cout << "Device name   : " << deviceString << std::endl;
	clGetDeviceInfo(deviceId, CL_DEVICE_VENDOR, sizeof(deviceString), &deviceString, NULL);
	std::cout << "Device vendor : " << deviceString << std::endl;
	clGetDeviceInfo(deviceId, CL_DRIVER_VERSION, sizeof(deviceString), &deviceString, NULL);
	std::cout << "Device version: " << deviceString << std::endl;

	// check if sharing is supported on the device
	size_t extensionSize;
	code = clGetDeviceInfo(deviceId, CL_DEVICE_EXTENSIONS, 0, nullptr, &extensionSize);
	CHECK_ERROR_CODE(clGetDeviceInfo);

	bool sharingSupported = false;

	if (extensionSize > 0)
	{
		char* extensions = new char[extensionSize];
		code = clGetDeviceInfo(deviceId, CL_DEVICE_EXTENSIONS, extensionSize, extensions, &extensionSize);
		CHECK_ERROR_CODE(clGetDeviceInfo);
		std::string stdDevString(extensions);
		delete extensions;

		size_t szOldPos = 0;
		size_t szSpacePos = stdDevString.find(' ', szOldPos); // extensions string is space delimited
		while (szSpacePos != stdDevString.npos)
		{
			if (strcmp(GL_SHARING_EXTENSION, stdDevString.substr(szOldPos, szSpacePos - szOldPos).c_str()) == 0)
			{
				// Device supports context sharing with OpenGL
				sharingSupported = true;
				break;
			}
			do
			{
				szOldPos = szSpacePos + 1;
				szSpacePos = stdDevString.find(' ', szOldPos);
			} while (szSpacePos == szOldPos);
		}
	}

	if (!sharingSupported)
	{
		std::cerr << "Sharing not supported" << std::endl;
		return EXIT_FAILURE;
	}

	// context
	cl_context_properties props[] =
	{
		CL_GL_CONTEXT_KHR,				reinterpret_cast<cl_context_properties>(glContext),
		DEVICE_CONTEXT_PROPERTY_NAME,	reinterpret_cast<cl_context_properties>(getCurrentDeviceContext()),
		CL_CONTEXT_PLATFORM,			reinterpret_cast<cl_context_properties>(platformId),
		0
	};
	cl_context gpuContext = clCreateContext(props, 1, &deviceId, nullptr, nullptr, &code);
	CHECK_ERROR_CODE(clCreateContext);

	// command queue
	cl_command_queue commandQueue = clCreateCommandQueue(gpuContext, deviceId, 0, &code);
	CHECK_ERROR_CODE(clCreateCommandQueue);

	// program
	std::string clProgramSource = readFile("cl/particle.cl");
	const char* clProgramSourceCStr = clProgramSource.c_str();
	cl_program program = clCreateProgramWithSource(gpuContext, 1, &clProgramSourceCStr, nullptr, &code);
	CHECK_ERROR_CODE(clCreateProgramWithSource);

	code = clBuildProgram(program, 0, nullptr, nullptr, nullptr, nullptr);
	CHECK_ERROR_CODE_LOG(clBuildProgram);

	// VBO
	const size_t NUM_PARTICLES = 1000000;
	size_t globalWorkSize[] = { NUM_PARTICLES };

	// create particle state buffer object
	GLuint particleStateVbo;
	glGenBuffers(1, &particleStateVbo);
	glBindBuffer(GL_ARRAY_BUFFER, particleStateVbo);

	unsigned int particleStateStructSize = 64;
	unsigned int particleStateSize = NUM_PARTICLES * particleStateStructSize;
	glBufferData(GL_ARRAY_BUFFER, particleStateSize, 0, GL_DYNAMIC_DRAW);

	cl_mem particleStateVboCl = clCreateFromGLBuffer(gpuContext, CL_MEM_WRITE_ONLY, particleStateVbo, nullptr);
	CHECK_ERROR_CODE(clCreateFromGLBuffer);

	float currentTime = 0;

	float particleSpawnRate = 200000.f;

	glFinish();

	// init particle state
	cl_kernel initParticleStateKernel = clCreateKernel(program, "initParticleState", &code);
	CHECK_ERROR_CODE_LOG(clCreateKernel);

	code = clSetKernelArg(initParticleStateKernel, 0, sizeof(cl_mem), (void*)&particleStateVboCl);
	CHECK_ERROR_CODE(clSetKernelArg);

	code = clEnqueueAcquireGLObjects(commandQueue, 1, &particleStateVboCl, 0, 0, 0);
	CHECK_ERROR_CODE(clEnqueueAcquireGLObjects);

	code = clEnqueueNDRangeKernel(commandQueue, initParticleStateKernel, 1, nullptr, globalWorkSize, nullptr, 0, 0, 0);
	CHECK_ERROR_CODE(clEnqueueNDRangeKernel);

	code = clEnqueueReleaseGLObjects(commandQueue, 1, &particleStateVboCl, 0, 0, 0);
	CHECK_ERROR_CODE(clEnqueueReleaseGLObjects);

	code = clFinish(commandQueue);
	CHECK_ERROR_CODE(clFinish);

	// spawn kernel
	cl_kernel spawnParticleKernel = clCreateKernel(program, "spawnParticle", &code);
	CHECK_ERROR_CODE_LOG(clCreateKernel);

	size_t spawnParticleKernelWorkGroupSize = 0;
	clGetKernelWorkGroupInfo(
		spawnParticleKernel,
		deviceId,
		CL_KERNEL_WORK_GROUP_SIZE,
		sizeof(size_t),
		(void*)&spawnParticleKernelWorkGroupSize,
		nullptr
	);

	code = clSetKernelArg(spawnParticleKernel, 0, sizeof(cl_mem), (void*)&particleStateVboCl);
	CHECK_ERROR_CODE(clSetKernelArg);
	code = clSetKernelArg(spawnParticleKernel, 1, spawnParticleKernelWorkGroupSize * sizeof(cl_uchar), nullptr);
	CHECK_ERROR_CODE(clSetKernelArg);

	// set update particle state kernel constant arguments
	cl_kernel updateParticleStateKernel = clCreateKernel(program, "updateParticleState", &code);
	CHECK_ERROR_CODE_LOG(clCreateKernel);

	code = clSetKernelArg(updateParticleStateKernel, 0, sizeof(cl_mem), (void*)&particleStateVboCl);
	CHECK_ERROR_CODE(clSetKernelArg);

	// check particle death conditions
	cl_kernel checkParticleDeathKernel = clCreateKernel(program, "checkParticleDeath", &code);
	CHECK_ERROR_CODE_LOG(clCreateKernel);

	code = clSetKernelArg(checkParticleDeathKernel, 0, sizeof(cl_mem), (void*)&particleStateVboCl);
	CHECK_ERROR_CODE(clSetKernelArg);

	Uint32 t1 = SDL_GetTicks();

	char windowTitle[128];

	// main loop
	SDL_Event event;
	Uint32 deltaTime = 0;
	bool loop = true;
	while (loop)
	{
		//std::cout << "Frame start ===================================================" << std::endl;
		const cl_float currentTimeSeconds = static_cast<cl_float>(t1) * 0.001f;
		const cl_float deltaTimeSeconds = static_cast<cl_float>(deltaTime) * 0.001f;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				loop = false;
				break;

			case SDL_KEYDOWN:
				switch (event.key.keysym.sym)
				{
				case SDLK_ESCAPE:
					loop = false;
					break;
				}
				break;

			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
				case SDL_WINDOWEVENT_RESIZED:
					updateWindowSize(event.window.data1, event.window.data2);
					break;
				}
			}
		}

		const Uint8* keyboardState = SDL_GetKeyboardState(NULL);
		if (keyboardState[SDL_SCANCODE_UP])
		{
			cameraPosition.s[2] += cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_DOWN])
		{
			cameraPosition.s[2] -= cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_O])
		{
			cameraPosition.s[1] += cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_L])
		{
			cameraPosition.s[1] -= cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_LEFT])
		{
			cameraPosition.s[0] += cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_RIGHT])
		{
			cameraPosition.s[0] -= cameraSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_I])
		{
			cameraElevation += cameraRotationSpeed * deltaTimeSeconds;
		}
		if (keyboardState[SDL_SCANCODE_K])
		{
			cameraElevation -= cameraRotationSpeed * deltaTimeSeconds;
		}
		
		updateCamera();

		// map OpenGL buffer object for writing from OpenCL
		glFinish();

		code = clEnqueueAcquireGLObjects(commandQueue, 1, &particleStateVboCl, 0, 0, 0);
		CHECK_ERROR_CODE(clEnqueueAcquireGLObjects);

		// prepare particles to spawn
		const cl_int numParticlesToSpawn = static_cast<cl_int>(std::ceil(particleSpawnRate * deltaTimeSeconds));

		if (numParticlesToSpawn > 0)
		{
			// spawn new particles
			code = clSetKernelArg(spawnParticleKernel, 2, sizeof(cl_int), &numParticlesToSpawn);
			CHECK_ERROR_CODE(clSetKernelArg);

			cl_int globalSeed = rand();
			code = clSetKernelArg(spawnParticleKernel, 3, sizeof(cl_int), &globalSeed);
			CHECK_ERROR_CODE(clSetKernelArg);

			code = clSetKernelArg(spawnParticleKernel, 4, sizeof(cl_float), &currentTimeSeconds);
			CHECK_ERROR_CODE(clSetKernelArg);

			code = clEnqueueNDRangeKernel(commandQueue, spawnParticleKernel, 1, nullptr, globalWorkSize, nullptr, 0, 0, 0);
			CHECK_ERROR_CODE(clEnqueueNDRangeKernel);
		}

		{
			// update the particles
			cl_int globalSeed = rand();
			code = clSetKernelArg(updateParticleStateKernel, 1, sizeof(cl_int), &globalSeed);
			CHECK_ERROR_CODE(clSetKernelArg);

			code = clSetKernelArg(updateParticleStateKernel, 2, sizeof(cl_float), &deltaTimeSeconds);
			CHECK_ERROR_CODE(clSetKernelArg);

			code = clEnqueueNDRangeKernel(commandQueue, updateParticleStateKernel, 1, nullptr, globalWorkSize, nullptr, 0, 0, 0);
			CHECK_ERROR_CODE(clEnqueueNDRangeKernel);

			// check the particles' death conditions
			code = clSetKernelArg(checkParticleDeathKernel, 1, sizeof(cl_float), &currentTimeSeconds);
			CHECK_ERROR_CODE(clSetKernelArg);

			code = clEnqueueNDRangeKernel(commandQueue, checkParticleDeathKernel, 1, nullptr, globalWorkSize, nullptr, 0, 0, 0);
			CHECK_ERROR_CODE(clEnqueueNDRangeKernel);
		}

		// unmap buffer objectS
		code = clEnqueueReleaseGLObjects(commandQueue, 1, &particleStateVboCl, 0, 0, 0);
		CHECK_ERROR_CODE(clEnqueueReleaseGLObjects);

		code = clFinish(commandQueue);
		CHECK_ERROR_CODE(clFinish);

		// opengl render
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(programId);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureId);
		glUniform1i(particleTextureUniform, 0);

		glUniformMatrix4fv(projectionMatrixUniform, 1, GL_FALSE, glm::value_ptr(projectionMatrix));
		glUniformMatrix4fv(modelViewMatrixUniform, 1, GL_FALSE, glm::value_ptr(modelViewMatrix));

		glEnableClientState(GL_VERTEX_ARRAY);

		glEnableVertexAttribArray(positionAttribute);
		glEnableVertexAttribArray(isAliveAttribute);

		glBindBuffer(GL_ARRAY_BUFFER, particleStateVbo);
		glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, particleStateStructSize, 0);
		glVertexAttribPointer(isAliveAttribute, 1, GL_UNSIGNED_BYTE, GL_FALSE, particleStateStructSize, (void*)16);

		glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);

		glDisableVertexAttribArray(positionAttribute);
		glDisableVertexAttribArray(isAliveAttribute);

		glDisableClientState(GL_VERTEX_ARRAY);

		glUseProgram(0);

		SDL_GL_SwapWindow(window);

		Uint32 t2 = SDL_GetTicks();
		deltaTime = t2 - t1;
		t1 = t2;
		sprintf_s(windowTitle, "%.1f fps", 1000.f / static_cast<float>(deltaTime));
		SDL_SetWindowTitle(window, windowTitle);
	}

	// release opencl stuff
	clReleaseContext(gpuContext);
	clReleaseCommandQueue(commandQueue);
	clReleaseMemObject(particleStateVboCl);
	clReleaseKernel(initParticleStateKernel);
	clReleaseKernel(spawnParticleKernel);
	clReleaseKernel(updateParticleStateKernel);
	clReleaseKernel(checkParticleDeathKernel);
	clReleaseProgram(program);

	// release opengl stuff
	glDeleteTextures(1, &textureId);
	glDeleteBuffers(1, &particleStateVbo);
	glDeleteShader(vertexShaderId);
	glDeleteShader(geometryShaderId);
	glDeleteShader(fragmentShaderId);
	glDeleteProgram(programId);

	// release sdl stuff
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return EXIT_SUCCESS;
}

// shaders
GLuint compileProgram(GLuint vertexShaderId, GLuint geometryShaderId, GLuint fragmentShaderId)
{
	GLuint programId = glCreateProgram();
	glAttachShader(programId, vertexShaderId);
	glAttachShader(programId, geometryShaderId);
	glAttachShader(programId, fragmentShaderId);
	glLinkProgram(programId);
	if (!checkProgram(programId))
	{
		glDeleteProgram(programId);
		return 0;
	}
	return programId;
}

bool checkProgram(GLuint programId)
{
	GLint result = GL_FALSE;
	glGetProgramiv(programId, GL_LINK_STATUS, &result);

	if (!result)
	{
		GLint infoLogLength;
		glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLogLength);
		GLchar* message = static_cast<GLchar*>(alloca(infoLogLength * sizeof(GLchar)));
		glGetProgramInfoLog(programId, infoLogLength, NULL, message);
		fprintf(stderr, "Warning: %s\n", message);
		return false;
	}

	return true;
}

GLuint loadShader(GLenum shaderType, const GLchar* source)
{
	GLuint shaderId = glCreateShader(shaderType);
	glShaderSource(shaderId, 1, &source, NULL);
	glCompileShader(shaderId);
	if (!checkShader(shaderId))
	{
		glDeleteShader(shaderId);
		return 0;
	}
	return shaderId;
}

bool checkShader(GLuint shaderId)
{
	GLint result = GL_FALSE;
	glGetShaderiv(shaderId, GL_COMPILE_STATUS, &result);

	if (!result)
	{
		GLint infoLogLength;
		glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
		GLchar* message = static_cast<GLchar*>(alloca(infoLogLength * sizeof(GLchar)));
		glGetShaderInfoLog(shaderId, infoLogLength, NULL, message);
		fprintf(stderr, "Warning: %s\n", message);
		return false;
	}

	return true;
}

// from https://stackoverflow.com/questions/24326432/convenient-way-to-show-opencl-error-codes
const char* getErrorString(cl_int error)
{
	switch (error) {
		// run-time and JIT compiler errors
	case 0: return "CL_SUCCESS";
	case -1: return "CL_DEVICE_NOT_FOUND";
	case -2: return "CL_DEVICE_NOT_AVAILABLE";
	case -3: return "CL_COMPILER_NOT_AVAILABLE";
	case -4: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
	case -5: return "CL_OUT_OF_RESOURCES";
	case -6: return "CL_OUT_OF_HOST_MEMORY";
	case -7: return "CL_PROFILING_INFO_NOT_AVAILABLE";
	case -8: return "CL_MEM_COPY_OVERLAP";
	case -9: return "CL_IMAGE_FORMAT_MISMATCH";
	case -10: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
	case -11: return "CL_BUILD_PROGRAM_FAILURE";
	case -12: return "CL_MAP_FAILURE";
	case -13: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
	case -14: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
	case -15: return "CL_COMPILE_PROGRAM_FAILURE";
	case -16: return "CL_LINKER_NOT_AVAILABLE";
	case -17: return "CL_LINK_PROGRAM_FAILURE";
	case -18: return "CL_DEVICE_PARTITION_FAILED";
	case -19: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";

		// compile-time errors
	case -30: return "CL_INVALID_VALUE";
	case -31: return "CL_INVALID_DEVICE_TYPE";
	case -32: return "CL_INVALID_PLATFORM";
	case -33: return "CL_INVALID_DEVICE";
	case -34: return "CL_INVALID_CONTEXT";
	case -35: return "CL_INVALID_QUEUE_PROPERTIES";
	case -36: return "CL_INVALID_COMMAND_QUEUE";
	case -37: return "CL_INVALID_HOST_PTR";
	case -38: return "CL_INVALID_MEM_OBJECT";
	case -39: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
	case -40: return "CL_INVALID_IMAGE_SIZE";
	case -41: return "CL_INVALID_SAMPLER";
	case -42: return "CL_INVALID_BINARY";
	case -43: return "CL_INVALID_BUILD_OPTIONS";
	case -44: return "CL_INVALID_PROGRAM";
	case -45: return "CL_INVALID_PROGRAM_EXECUTABLE";
	case -46: return "CL_INVALID_KERNEL_NAME";
	case -47: return "CL_INVALID_KERNEL_DEFINITION";
	case -48: return "CL_INVALID_KERNEL";
	case -49: return "CL_INVALID_ARG_INDEX";
	case -50: return "CL_INVALID_ARG_VALUE";
	case -51: return "CL_INVALID_ARG_SIZE";
	case -52: return "CL_INVALID_KERNEL_ARGS";
	case -53: return "CL_INVALID_WORK_DIMENSION";
	case -54: return "CL_INVALID_WORK_GROUP_SIZE";
	case -55: return "CL_INVALID_WORK_ITEM_SIZE";
	case -56: return "CL_INVALID_GLOBAL_OFFSET";
	case -57: return "CL_INVALID_EVENT_WAIT_LIST";
	case -58: return "CL_INVALID_EVENT";
	case -59: return "CL_INVALID_OPERATION";
	case -60: return "CL_INVALID_GL_OBJECT";
	case -61: return "CL_INVALID_BUFFER_SIZE";
	case -62: return "CL_INVALID_MIP_LEVEL";
	case -63: return "CL_INVALID_GLOBAL_WORK_SIZE";
	case -64: return "CL_INVALID_PROPERTY";
	case -65: return "CL_INVALID_IMAGE_DESCRIPTOR";
	case -66: return "CL_INVALID_COMPILER_OPTIONS";
	case -67: return "CL_INVALID_LINKER_OPTIONS";
	case -68: return "CL_INVALID_DEVICE_PARTITION_COUNT";

		// extension errors
	case -1000: return "CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR";
	case -1001: return "CL_PLATFORM_NOT_FOUND_KHR";
	case -1002: return "CL_INVALID_D3D10_DEVICE_KHR";
	case -1003: return "CL_INVALID_D3D10_RESOURCE_KHR";
	case -1004: return "CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR";
	case -1005: return "CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR";
	default: return "Unknown OpenCL error";
	}
}

std::string getErrorLog(cl_program program, cl_device_id deviceId)
{
	cl_int code;
	size_t errorLogLength;
	code = clGetProgramBuildInfo(program, deviceId, CL_PROGRAM_BUILD_LOG, 0, NULL, &errorLogLength);
	assert(code == 0);

	std::string errorLog;
	errorLog.resize(errorLogLength);

	code = clGetProgramBuildInfo(program, deviceId, CL_PROGRAM_BUILD_LOG, errorLogLength, (void*)errorLog.data(), NULL);
	assert(code == 0);

	return errorLog;
}

std::string readFile(const std::string& filePath)
{
	std::ifstream file(filePath.c_str(), std::ifstream::binary);
	if (!file.is_open())
	{
		std::cerr << "Warning: unable to open file '" << filePath << "'" << std::endl;
		return "";
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

GLuint loadImage(const std::string& filePath)
{
	SDL_Surface* surface = IMG_Load(filePath.c_str());

	if (surface == nullptr)
	{
		std::cerr << "Could not load image '" << filePath << "'" << std::endl;
		return 0;
	}

	GLuint textureId = 0;
	glGenTextures(1, &textureId);
	if (textureId == 0)
	{
		std::cerr << "glGenTextures failed" << std::endl;
		return 0;
	}

	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);

	SDL_FreeSurface(surface);

	return textureId;
}
