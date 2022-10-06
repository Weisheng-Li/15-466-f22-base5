#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <random>
#include <chrono>
#include <thread>

GLuint phonebank_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > phonebank_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("plane.pnct"));
	phonebank_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > phonebank_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("plane.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = phonebank_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = phonebank_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

WalkMesh const *walkmesh = nullptr;
Load< WalkMeshes > phonebank_walkmeshes(LoadTagDefault, []() -> WalkMeshes const * {
	WalkMeshes *ret = new WalkMeshes(data_path("plane.w"));
	walkmesh = &ret->lookup("WalkMesh");
	return ret;
});

PlayMode::PlayMode() : scene(*phonebank_scene) {
	for (auto &transform : scene.transforms) {
		if (transform.name == "Cone") target = &transform;
	}

	//create a player transform:
	scene.transforms.emplace_back();
	player.transform = &scene.transforms.back();
	player.transform->position = start_pos;

	//create a player camera attached to a child of the player transform:
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	player.camera = &scene.cameras.back();
	player.camera->fovy = glm::radians(60.0f);
	player.camera->near = 0.01f;
	player.camera->transform->parent = player.transform;

	//player's eyes are 1.8 units above the ground:
	player.camera->transform->position = glm::vec3(0.0f, 0.0f, 1.8f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	player.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start player walking at nearest walk point:
	player.at = walkmesh->nearest_walk_point(player.transform->position);

}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			glm::vec3 upDir = walkmesh->to_world_smooth_normal(player.at);
			player.transform->rotation = glm::angleAxis(-motion.x * player.camera->fovy, upDir) * player.transform->rotation;

			float pitch = glm::pitch(player.camera->transform->rotation);
			pitch += motion.y * player.camera->fovy;
			//camera looks down -z (basically at the player's feet) when pitch is at zero.
			pitch = std::min(pitch, 0.95f * 3.1415926f);
			pitch = std::max(pitch, 0.05f * 3.1415926f);
			player.camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

			return true;
		}
	}

	return false;
}

int16_t PlayMode::pos_to_layout(glm::vec3 player_pos) {
	// checker board has grid_size * grid_size grid cells
	const unsigned int grid_size = 4;
	const unsigned int grid_cell_size = 5;

	// define initial player position (also defined in)
	glm::vec2 offset = glm::vec2(player_pos - start_pos);

	glm::uvec2 quantized_offset = glm::vec2(floor(offset.x)/grid_cell_size, floor(offset.y)/grid_cell_size);
	quantized_offset.x = quantized_offset.x >= grid_size? grid_size - 1 : quantized_offset.x;
	quantized_offset.y = quantized_offset.y >= grid_size? grid_size - 1 : quantized_offset.y;

	const glm::uvec2 start_layout = glm::uvec2(0, grid_size - 1);
	return layout[start_layout.y - quantized_offset.y][start_layout.x + quantized_offset.x];
}

void PlayMode::update(float elapsed) {
	if (countdown > 0.0f) {
		countdown -= elapsed;
		if (countdown <= 0) {
			countdown = 0.0f;
			reset_game();
		}
		return;
	}

	//player walking:
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 3.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		//get move in world coordinate system:
		glm::vec3 remain = player.transform->make_local_to_world() * glm::vec4(move.x, move.y, 0.0f, 0.0f);

		//using a for() instead of a while() here so that if walkpoint gets stuck in
		// some awkward case, code will not infinite loop:
		for (uint32_t iter = 0; iter < 10; ++iter) {
			if (remain == glm::vec3(0.0f)) break;
			WalkPoint end;
			float time;
			walkmesh->walk_in_triangle(player.at, remain, &end, &time);
			player.at = end;
			if (time == 1.0f) {
				//finished within triangle:
				remain = glm::vec3(0.0f);
				break;
			}
			//some step remains:
			remain *= (1.0f - time);
			//try to step over edge:
			glm::quat rotation;
			if (walkmesh->cross_edge(player.at, &end, &rotation)) {
				//stepped to a new triangle:
				player.at = end;
				//rotate step to follow surface:
				remain = rotation * remain;
			} else {
				//ran into a wall, bounce / slide along it:
				glm::vec3 const &a = walkmesh->vertices[player.at.indices.x];
				glm::vec3 const &b = walkmesh->vertices[player.at.indices.y];
				glm::vec3 const &c = walkmesh->vertices[player.at.indices.z];
				glm::vec3 along = glm::normalize(b-a);
				glm::vec3 normal = glm::normalize(glm::cross(b-a, c-a));
				glm::vec3 in = glm::cross(normal, along);

				//check how much 'remain' is pointing out of the triangle:
				float d = glm::dot(remain, in);
				if (d < 0.0f) {
					//bounce off of the wall:
					remain += (-1.25f * d) * in;
				} else {
					//if it's just pointing along the edge, bend slightly away from wall:
					remain += 0.01f * d * in;
				}
			}
		}

		if (remain != glm::vec3(0.0f)) {
			std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
		}

		//update player's position to respect walking:
		player.transform->position = walkmesh->to_world_point(player.at);
		{ //update player's rotation to respect local (smooth) up-vector:
			
			glm::quat adjust = glm::rotation(
				player.transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
				walkmesh->to_world_smooth_normal(player.at) //smoothed up vector at walk location
			);
			player.transform->rotation = glm::normalize(adjust * player.transform->rotation);
		}

		current_state = pos_to_layout(player.transform->position);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	player.camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	{	// set background color based on current state
		const std::vector<glm::vec3> color_pallete = {
			glm::vec3(0.9176f, 0.8828f, 0.7176f), // cream
			glm::vec3(0.9882f, 0.7490f, 0.2863f), // yellow
			glm::vec3(0.9686f, 0.4980f, 0.0000f), // orange
			glm::vec3(0.8392f, 0.1569f, 0.1569f), // red
			glm::vec3(0.0000f, 0.1882f, 0.2863f)  // dark blue
		};
		size_t pallete_size = color_pallete.size();
		glm::vec3 current_color;
		

		if (current_state == -1) current_color = color_pallete[pallete_size - 1];
		else if (current_state == 100) current_color = color_pallete[1];
		else {
			assert(current_state < static_cast<int16_t>(pallete_size));
			current_color = color_pallete[current_state];
		}

		glClearColor(current_color.x, current_color.y, current_color.z, 1.0f);
	}

	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*player.camera);

	// In case you are wondering if your walkmesh is lining up with your scene, try:
	// {
	// 	glDisable(GL_DEPTH_TEST);
	// 	DrawLines lines(player.camera->make_projection() * glm::mat4(player.camera->transform->make_world_to_local()));
	// 	for (auto const &tri : walkmesh->triangles) {
	// 		lines.draw(walkmesh->vertices[tri.x], walkmesh->vertices[tri.y], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 		lines.draw(walkmesh->vertices[tri.y], walkmesh->vertices[tri.z], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 		lines.draw(walkmesh->vertices[tri.z], walkmesh->vertices[tri.x], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 	}
	// }

	glDisable(GL_DEPTH_TEST);
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	DrawLines lines(glm::mat4(
		1.0f / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	));

	if (current_state == -1) {
		// game over
		constexpr float H2 = 0.3f;
		lines.draw_text("Game Over",
		glm::vec3(-aspect + 4.0f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		glm::u8vec4(0xff, 0xff, 0xff, 0xff));

		if (countdown == 0.0f)
			countdown = 2.0f;
	} else if (current_state == 100) {
		// game over
		constexpr float H2 = 0.3f;
		lines.draw_text("You've reached the",
		glm::vec3(-aspect + 2.5f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		glm::u8vec4(0x0, 0x0, 0x0, 0x0));
		lines.draw_text("Destination",
		glm::vec3(-aspect + 4.0f * H2, -1.0 + 3.5f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		glm::u8vec4(0x0, 0x0, 0x0, 0x0));

		if (countdown == 0.0f) {
			countdown = 2.0f;
			target->position = target->position + glm::vec3(0, 0, -100);
		}
	} else {
		constexpr float H2 = 0.3f;
		glm::u8vec4 color;
		if (current_state < 2) color = glm::u8vec4(0x0, 0x0, 0x0, 0x0);
		else 				   color = glm::u8vec4(0xff, 0xff, 0xff, 0x00);

		lines.draw_text(std::to_string(current_state),
		glm::vec3(-aspect + 5.5f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		color);
	}

	GL_ERRORS();
}

void PlayMode::reset_game() {
	if (target->position.z <= -50) target->position += glm::vec3(0, 0, 100);

	// reset player
	player.transform->position = start_pos;
	player.transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	player.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start player walking at nearest walk point:
	player.at = walkmesh->nearest_walk_point(player.transform->position);

	current_state = pos_to_layout(player.transform->position);
}
