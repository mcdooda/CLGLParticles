// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

typedef struct { ulong state; ulong inc; } pcg32_random_t;

uint pcg32_random_r(pcg32_random_t* rng)
{
	ulong oldstate = rng->state;
	// Advance internal state
	rng->state = oldstate * 6364136223846793005UL + (rng->inc | 1);
	// Calculate output function (XSH RR), uses old state for max ILP
	uint xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
	uint rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void pcg32_srandom_r(pcg32_random_t* rng, ulong initstate, ulong initseq)
{
	rng->state = 0U;
	rng->inc = (initseq << 1u) | 1u;
	pcg32_random_r(rng);
	rng->state += initstate;
	pcg32_random_r(rng);
}

//////

const float3 initialPosition = (float3)(0.f, 20.f, 0.f);
const float3 initialVelocity = (float3)(0.f, 0.f, 0.f);

typedef struct
{
	float3 position __attribute__((aligned(16)));
	float3 velocity __attribute__((aligned(16)));
	float spawnTime __attribute__((aligned(4)));
	uchar isAlive __attribute__((aligned(1)));
} __attribute__((aligned(64))) ParticleState;

float3 rotateVector(float3 v, float3 k, float theta)
{
	float cos_theta = cos(theta);
	float sin_theta = sin(theta);

	return (v * cos_theta) + (cross(k, v) * sin_theta) + (k * dot(k, v)) * (1 - cos_theta);
}

typedef pcg32_random_t RngValue;
typedef RngValue* Rng;

void randomInit(Rng rng, int globalSeed)
{
	ulong initState = globalSeed;
	ulong initSeq = get_global_id(0);
	pcg32_srandom_r(rng, initState, initSeq);
}

uint randomUint(Rng rng)
{
	return pcg32_random_r(rng);
}

float random01(Rng rng)
{
	return (float)((double)randomUint(rng) / UINT_MAX);
}

float random(Rng rng, float min, float max)
{
	float randomFloat = random01(rng);
	return min + randomFloat * (max - min);
}

__kernel void initParticleState(__global ParticleState* particles)
{
	size_t id = get_global_id(0);
	__global ParticleState* particle = &particles[id];
	particle->position = initialPosition;
	particle->velocity = initialVelocity;
	particle->isAlive = 0;
}

// uniform cylinder distribution
void initRandomOnCylinder(__global ParticleState* particle, float radius, float height, Rng rng)
{
	float randomAngle = random(rng, 0.f, M_PI_F * 2.f);
	float randomRadius = sqrt(random(rng, 0.f, 1.f)) * radius;
	float randomY = random(rng, height * -0.5f, height * 0.5f);
	particle->position.x = cos(randomAngle) * randomRadius;
	particle->position.y = randomY;
	particle->position.z = sin(randomAngle) * randomRadius;
}

// non uniform sphere surface distribution
void initRandomOnSphere(__global ParticleState* particle, float radius, Rng rng)
{
	float x = random(rng, -1.f, 1.f);
	float y = random(rng, -1.f, 1.f);
	float z = random(rng, -1.f, 1.f);
	const float length = sqrt(x * x + y * y + z * z);
	particle->position.x = x / length * radius;
	particle->position.y = y / length * radius;
	particle->position.z = z / length * radius;
}

__kernel void spawnParticle(
	__global ParticleState* particles,
	__local uchar* canSpawnParticles,
	uint numParticlesToSpawn,
	int globalSeed,
	float currentTime)
{
	size_t id = get_global_id(0);
	size_t localId = get_local_id(0);
	__global ParticleState* particle = &particles[id];
	canSpawnParticles[localId] = !particle->isAlive;

	RngValue rng;
	randomInit(&rng, globalSeed);

	barrier(CLK_LOCAL_MEM_FENCE);

	if (localId == 0)
	{
		size_t localSize = get_local_size(0);
		size_t groupId = get_group_id(0);
		size_t numGroups = get_num_groups(0);

		uint numParticleToSpawnForWorkGroup = numParticlesToSpawn / get_num_groups(0);
		if ((groupId + globalSeed) % numGroups < numParticlesToSpawn % numGroups)
		{
			++numParticleToSpawnForWorkGroup;
		}
		numParticleToSpawnForWorkGroup = min(numParticleToSpawnForWorkGroup, (uint)localSize);

		uint numSpawnedParticles = 0;

		for (uint i = 0; i < localSize; ++i)
		{
			if (canSpawnParticles[i])
			{
				if (numSpawnedParticles < numParticleToSpawnForWorkGroup)
				{
					++numSpawnedParticles;
				}
				else
				{
					canSpawnParticles[i] = 0;
				}
			}
		}
	}

	barrier(CLK_LOCAL_MEM_FENCE);

	if (canSpawnParticles[localId])
	{
		particle->velocity = (float3)(0.f, 0.f, 0.f);
		particle->spawnTime = currentTime;
		particle->isAlive = 1;

		initRandomOnCylinder(particle, 45.f, 0.f, &rng);
		//initRandomOnSphere(particle, 100.f, &rng);
		//particle->position = (float3)(0.f, 0.f, 0.f);
	}
}

float remap(float value, float min1, float max1, float min2, float max2)
{
	return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

void updateVortex(__global ParticleState* particle, float minRadius, float minRadiusAngularSpeed, float maxRadius, float maxRadiusAngularSpeed, float deltaTime)
{
	const float radius = sqrt(particle->position.x * particle->position.x + particle->position.z * particle->position.z);
	float angularSpeed = remap(radius, minRadius, maxRadius, minRadiusAngularSpeed, maxRadiusAngularSpeed);
	float angle = angularSpeed * deltaTime;
	particle->position = rotateVector(particle->position, (float3)(0.f, 1.f, 0.f), angle);
}

void updateRadial(__global ParticleState* particle, float minRadius, float minRadiusSpeed, float maxRadius, float maxRadiusSpeed, float deltaTime)
{
	const float radius = sqrt(particle->position.x * particle->position.x + particle->position.z * particle->position.z);
	float speed = remap(radius, minRadius, maxRadius, minRadiusSpeed, maxRadiusSpeed);
	float3 velocity = particle->position * speed;
	particle->position += velocity * deltaTime;
}

void accelerate(__global ParticleState* particle, float3 direction, float deltaTime)
{
	particle->velocity += direction * deltaTime;
}

void applyVelocity(__global ParticleState* particle, float deltaTime)
{
	particle->position += particle->velocity * deltaTime;
}

__kernel void updateParticleState(
	__global ParticleState* particles,
	int globalSeed,
	float deltaTime)
{
	size_t id = get_global_id(0);
	__global ParticleState* particle = &particles[id];
	if (!particle->isAlive)
	{
		return;
	}

	RngValue rng;
	randomInit(&rng, globalSeed);

	//updateVortex(particle, 0.f, -2.f, 50.f, 0.f, deltaTime);
	//updateRadial(particle, 0.f, -0.6f, 50.f, 0.f, deltaTime);

	float accelerationX = random(&rng, -50.f, 50.f);
	float accelerationY = random(&rng, -5.f, -10.f);
	float accelerationZ = random(&rng, -50.f, 50.f);
	float3 acceleration = (float3)(accelerationX, accelerationY, accelerationZ);
	accelerate(particle, acceleration, deltaTime);

	//accelerate(particle, (float3)(0.f, -10.f, 0.f), deltaTime);

	applyVelocity(particle, deltaTime);
}

bool checkAge(__global ParticleState* particle, float currentTime, float maxAge)
{
	return currentTime - particle->spawnTime >= maxAge;
}

__kernel void checkParticleDeath(__global ParticleState* particles, float currentTime)
{
	size_t id = get_global_id(0);
	__global ParticleState* particle = &particles[id];
	if (!particle->isAlive)
	{
		return;
	}

	if (checkAge(particle, currentTime, 5.f))
	{
		particle->isAlive = 0;
		particle->position = initialPosition;
	}
}