#include "Mode.hpp"

#include "Scene.hpp"
#include "WalkMesh.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;
	Scene::Transform *target = nullptr;

	int16_t pos_to_layout(glm::vec3 player_pos);
	std::array<std::array<int16_t, 4>, 4> layout = {{
		{1, 1, 1, 100}, 
		{-1, 2, -1, 2}, 
		{1, 3, 3, -1}, 
		{0, 1, -1, 2}
	}};

	int16_t current_state;
	float countdown = 0.0f;

	const glm::vec3 start_pos = glm::vec3(-10.0f, -10.0f, 0.0f);

	void reset_game();

	//player info:
	struct Player {
		WalkPoint at;
		//transform is at player's feet and will be yawed by mouse left/right motion:
		Scene::Transform *transform = nullptr;
		//camera is at player's head and will be pitched by mouse up/down motion:
		Scene::Camera *camera = nullptr;
	} player;
};
