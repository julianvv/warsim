#include "precomp.h" // include (only) this in every .cpp file


#define NUM_TANKS_BLUE 1279
#define NUM_TANKS_RED 1279

#define TANK_MAX_HEALTH 1000
#define ROCKET_HIT_VALUE 60
#define PARTICLE_BEAM_HIT_VALUE 50

#define TANK_MAX_SPEED 1.5

#define HEALTH_BARS_OFFSET_X 0
#define HEALTH_BAR_HEIGHT 70
#define HEALTH_BAR_WIDTH 1
#define HEALTH_BAR_SPACING 0

#define MAX_FRAMES 2000

//Global performance timer
#define REF_PERFORMANCE 45501 //UPDATE THIS WITH YOUR REFERENCE PERFORMANCE (see console after 2k frames)
static timer perf_timer;
static float duration;

//Load sprite files and initialize sprites
static Surface* background_img = new Surface("assets/Background_Grass.png");
static Surface* tank_red_img = new Surface("assets/Tank_Proj2.png");
static Surface* tank_blue_img = new Surface("assets/Tank_Blue_Proj2.png");
static Surface* rocket_red_img = new Surface("assets/Rocket_Proj2.png");
static Surface* rocket_blue_img = new Surface("assets/Rocket_Blue_Proj2.png");
static Surface* particle_beam_img = new Surface("assets/Particle_Beam.png");
static Surface* smoke_img = new Surface("assets/Smoke.png");
static Surface* explosion_img = new Surface("assets/Explosion.png");

static Sprite background(background_img, 1);
static Sprite tank_red(tank_red_img, 12);
static Sprite tank_blue(tank_blue_img, 12);
static Sprite rocket_red(rocket_red_img, 12);
static Sprite rocket_blue(rocket_blue_img, 12);
static Sprite smoke(smoke_img, 4);
static Sprite explosion(explosion_img, 9);
static Sprite particle_beam_sprite(particle_beam_img, 3);

const static vec2 tank_size(14, 18);
const static vec2 rocket_size(25, 24);

const static float tank_radius = 8.5f;
const static float rocket_radius = 10.f;

std::mutex rockets_lock;
std::mutex spatial_lock;
const static int thread_amount = thread::hardware_concurrency();
ThreadPool thread_pool(thread_amount);


// -----------------------------------------------------------
// Initialize the application
// -----------------------------------------------------------
void Game::init()
{
    sh_blue = new spatial_hash(3000, 3000, 36, 500, 500);
    sh_red = new spatial_hash(3000, 3000, 36, 500, 500);
    frame_count_font = new Font("assets/digital_small.png", "ABCDEFGHIJKLMNOPQRSTUVWXYZ:?!=-0123456789.");

    tanks.reserve(NUM_TANKS_BLUE + NUM_TANKS_RED);
    tank_pointers.reserve(NUM_TANKS_BLUE + NUM_TANKS_RED);

    uint rows = (uint)sqrt(NUM_TANKS_BLUE + NUM_TANKS_RED);
    uint max_rows = 12;

    float start_blue_x = tank_size.x + 10.0f;
    float start_blue_y = tank_size.y + 80.0f;

    float start_red_x = 980.0f;
    float start_red_y = 100.0f;

    float spacing = 15.0f;

    //Spawn blue tanks
    for (int i = 0; i < NUM_TANKS_BLUE; i++)
    {
        Tank tank = Tank(start_blue_x + ((i % max_rows) * spacing), start_blue_y + ((i / max_rows) * spacing), BLUE, &tank_blue, &smoke, 1200, 600, tank_radius, TANK_MAX_HEALTH, TANK_MAX_SPEED);
        tanks.push_back(tank);
        sh_blue->add_tank(&tank);
    }
    //Spawn red tanks
    for (int i = 0; i < NUM_TANKS_RED; i++)
    {
        Tank tank = Tank(start_red_x + ((i % max_rows) * spacing), start_red_y + ((i / max_rows) * spacing), RED, &tank_red, &smoke, 80, 80, tank_radius, TANK_MAX_HEALTH, TANK_MAX_SPEED);
        tanks.push_back(tank);
        sh_red->add_tank(&tank);
    }

	for (int i = 0; i < NUM_TANKS_BLUE + NUM_TANKS_RED; i++)
	{
        tank_pointers.push_back(&tanks.at(i));
	}
	
    particle_beams.push_back(Particle_beam(vec2(SCRWIDTH / 2, SCRHEIGHT / 2), vec2(100, 50), &particle_beam_sprite, PARTICLE_BEAM_HIT_VALUE));
    particle_beams.push_back(Particle_beam(vec2(80, 80), vec2(100, 50), &particle_beam_sprite, PARTICLE_BEAM_HIT_VALUE));
    particle_beams.push_back(Particle_beam(vec2(1200, 600), vec2(100, 50), &particle_beam_sprite, PARTICLE_BEAM_HIT_VALUE));
}

// -----------------------------------------------------------
// Close down application
// -----------------------------------------------------------
void Game::shutdown()
{
    free(sh_blue);
    free(sh_red);
}

// -----------------------------------------------------------
// Iterates through all tanks and returns the closest enemy tank for the given tank
// -----------------------------------------------------------
Tank& Game::find_closest_enemy(Tank& current_tank)
{
    float closest_distance = numeric_limits<float>::infinity();
    int closest_index = 0;
    
    for (int i = 0; i < tanks.size(); i++)
    {
        if (tanks.at(i).allignment != current_tank.allignment && tanks.at(i).active)
        {
            float sqr_dist = fabsf((tanks.at(i).get_position() - current_tank.get_position()).sqr_length());
            if (sqr_dist < closest_distance)
            {
                closest_distance = sqr_dist;
                closest_index = i;
            }
        }
    }
    return tanks.at(closest_index);
}

void Game::check_rocket_collision(Rocket& rocket, spatial_hash* sh)
{
    spatial_cell* cell;
    if ((rocket.position.x > -100 && rocket.position.x < 1380) && (rocket.position.y > -100 && rocket.position.y < 820))
    {
        for (int x = -1; x <= 1; x++)
        {
            for (int y = -1; y <= 1; y++)
            {
                cell = sh->position_to_cell(rocket.position.x + (x * sh->get_cell_size()), rocket.position.y + (y * sh->get_cell_size()));
                for (int i = 0; i < cell->size(); i++)
                {
                    Tank* tank = cell->at(i);
                    if (tank->active && rocket.intersects(tank->position, tank->collision_radius))
                    {
                        explosions.push_back(Explosion(&explosion, tank->position));

                        if (tank->hit(ROCKET_HIT_VALUE))
                        {
                            smokes.push_back(Smoke(smoke, tank->position - vec2(0, 48)));
                            tank->active = false;
                            sh->remove_tank(tank);
                        }

                        rocket.active = false;
                        return;
                    }
                }
            }
        }
    }
    else
    {
        rocket.active = false;
    }
}

void Game::remove_inactive_rockets()
{
    //Remove exploded rockets with remove erase idiom
    rockets.erase(std::remove_if(rockets.begin(), rockets.end(), [](const Rocket& rocket) { return !rocket.active; }), rockets.end());
}


void Game::update_tanks()
{
    int max = tanks.size();
    int thread_size = max / thread_amount;
    int start = 0;
    int end = start + thread_size;
    int remaining = max % thread_amount;
    int currently_remaining = remaining;

    vector<future<void>> futures;

    //Check positioning and nudge them away
    for(int i = 0; i < thread_amount; i++)
    {
        if (currently_remaining > 0)
        {
            end++;
            currently_remaining--;
        }
        futures.push_back(thread_pool.enqueue([&, start, end]
            {
		        for(int j = start; j < end; j++)
		        {
                    Tank& tank = tanks.at(j);
                    if(tank.active)
                    {
                        spatial_hash* sh;
                        spatial_cell* cell;
          
                    	if(tank.allignment == BLUE)
                    	{
                            sh = sh_blue;
                    	}else
                    	{
                            sh = sh_red;
                    	}
          
                    	for(int x = -1; x <= 1; x++)
                    	{
                    		for(int y = -1; y <=1; y++)
                    		{
                                cell = sh->position_to_cell(tank.position.x + (x * sh->get_cell_size()), tank.position.y + (y * sh->get_cell_size()));
                    			for(int cell_nr = 0; cell_nr < cell->size(); cell_nr++)
                    			{
                                    Tank& o_tank = *cell->at(cell_nr);
                                    if (&tank == &o_tank || !&o_tank.active) continue;
          
                                    vec2 dir = tank.get_position() - o_tank.get_position();
                                    float dir_squared_len = dir.sqr_length();
                                    float col_squared_len = (tank.get_collision_radius() + o_tank.get_collision_radius());
                                    col_squared_len *= col_squared_len;
          
                    				if(dir_squared_len < col_squared_len)
                    				{
                                        tank.push(dir.normalized(), 1.f);
                    				}
                    			}
                    		}
                    	}
                        vec2 direction = (tank.target - tank.position).normalized();
          
                        tank.speed = direction + tank.force;

                    	

                    	//Reload tank and update sprite
                        tank.tick();

                    	//Shoot at closest target if reloaded
                        if (tank.rocket_reloaded())
                        {
                            Tank& target = find_closest_enemy(tank);

                            {
                                std::unique_lock<std::mutex> lock(rockets_lock);
                                rockets.push_back(Rocket(tank.position, (target.get_position() - tank.position).normalized() * 3, rocket_radius, tank.allignment, ((tank.allignment == RED) ? &rocket_red : &rocket_blue)));
                            }
                        	
                            tank.reload_rocket();
                        }
                    }
		        }
            }));

        start = end;
        end += thread_size;
    }

	for(future<void>& fut : futures)
	{
        fut.wait();
	}

	for(Tank& tank : tanks)
	{
		if(tank.active)
		{
            spatial_hash* sh;
            if (tank.allignment == BLUE)
            {
                sh = sh_blue;
            }
            else
            {
                sh = sh_red;
            }

            sh->move_tank(&tank, tank.position.x + tank.speed.x * tank.max_speed * 0.5f, tank.position.y + tank.speed.y * tank.max_speed * 0.5f);

		}
	}
}

void Game::update_smokes()
{
    int max = smokes.size();//1276
    int thread_size = max / thread_amount; //106
    int start = 0; 
    int end = start + thread_size; 
    int remaining = max % thread_amount;//4
    int currently_remaining = remaining;

    vector<future<void>> futures;
    for (int threadnr = 0; threadnr < thread_amount; threadnr++)
    {
	    if(currently_remaining > 0)
	    {
            end++;
            currently_remaining--;
	    }
        futures.push_back(thread_pool.enqueue([&, start, end]
            {
                for(int smokenr = start; smokenr < end; smokenr++)
                {
                    smokes.at(smokenr).tick();
                }
            }));
        start = end;
        end += thread_size;
    }

    for (future<void>& fut : futures)
    {
        fut.wait();
    }    
}

void Game::update_explosions()
{
    int max = explosions.size();
    int thread_size = max / thread_amount;
    int start = 0;
    int end = start + thread_size; //100
    int remaining = max % thread_amount;
    int currently_remaining = remaining;

    vector<future<void>> futures;
    for (int threadnr = 0; threadnr < thread_amount; threadnr++)
    {
        if (currently_remaining > 0)
        {
            end++;
            currently_remaining--;
        }
        futures.push_back(thread_pool.enqueue([&, start, end]
            {
                for (int explosionnr = start; explosionnr < end; explosionnr++)
                {
                    explosions.at(explosionnr).tick();
                }
            }));
        start = end;
        end += thread_size;
    }

    for (future<void>& fut : futures)
    {
        fut.wait();
    }
    start = 0;
    end = start + thread_size;
    currently_remaining = remaining;

    // for (int threadnr = 0; threadnr < thread_amount; threadnr++)
    // {
    //     if (currently_remaining > 0)
    //     {
    //         end++;
    //         currently_remaining--;
    //     }
    //     futures.push_back(thread_pool.enqueue([&, start, end]
    //         {
    //             explosions.erase(std::remove_if(explosions.begin() + start, explosions.begin() + end, [](const Explosion& explosion) { return explosion.done(); }), explosions.begin() + end);
    //         }));
    //     start = end;
    //     end += thread_size;
    // }
    //
    // for (future<void>& fut : futures)
    // {
    //     fut.wait();
    // }
    //
    // futures.clear();

    explosions.erase(std::remove_if(explosions.begin(), explosions.end(), [](const Explosion& explosion) { return explosion.done(); }), explosions.end());
}


// -----------------------------------------------------------
// Update the game state:
// Move all objects
// Update sprite frames
// Collision detection
// Targeting etc..
// -----------------------------------------------------------
void Game::update(float deltaTime)
{
    update_tanks();
    update_smokes();
    
    //Update rockets
    for (Rocket& rocket : rockets)
    {
        rocket.tick();
        spatial_hash* sh;
    	if(rocket.allignment == BLUE)
    	{
            sh = sh_red;
    	}else
    	{
            sh = sh_blue;
    	}

        check_rocket_collision(rocket, sh);
    }
    //remove inactive rockets after they've all been updated
    remove_inactive_rockets();
    
    //Update particle beams
    for (Particle_beam& particle_beam : particle_beams)
    {
        particle_beam.tick(tanks);

        //Damage all tanks within the damage window of the beam (the window is an axis-aligned bounding box)
        for (Tank& tank : tanks)
        {
            if (tank.active && particle_beam.rectangle.intersects_circle(tank.get_position(), tank.get_collision_radius()))
            {
                if (tank.hit(particle_beam.damage))
                {
                    smokes.push_back(Smoke(smoke, tank.position - vec2(0, 48)));
                }
            }
        }
    }

    update_explosions();
}

void Game::draw()
{
    // clear the graphics window
    screen->clear(0);

    //Draw background
    background.draw(screen, 0, 0);

    //Draw sprites
    for (int i = 0; i < NUM_TANKS_BLUE + NUM_TANKS_RED; i++)
    {
        tanks.at(i).draw(screen);

        vec2 tank_pos = tanks.at(i).get_position();
        // tread marks
        if ((tank_pos.x >= 0) && (tank_pos.x < SCRWIDTH) && (tank_pos.y >= 0) && (tank_pos.y < SCRHEIGHT))
            background.get_buffer()[(int)tank_pos.x + (int)tank_pos.y * SCRWIDTH] = sub_blend(background.get_buffer()[(int)tank_pos.x + (int)tank_pos.y * SCRWIDTH], 0x808080);
    }

    for (Rocket& rocket : rockets)
    {
        rocket.draw(screen);
    }

    for (Smoke& smoke : smokes)
    {
        smoke.draw(screen);
    }

    for (Particle_beam& particle_beam : particle_beams)
    {
        particle_beam.draw(screen);
    }

    for (Explosion& explosion : explosions)
    {
        explosion.draw(screen);
    }

    //Draw sorted health bars
    for (int t = 0; t < 2; t++)
    {
        const int NUM_TANKS = ((t < 1) ? NUM_TANKS_BLUE : NUM_TANKS_RED);
        const int begin = ((t < 1) ? 0 : NUM_TANKS_BLUE);
        std::vector<int> sorted_health = count_sort(tank_pointers, begin, NUM_TANKS);

        for (int i = 0; i < NUM_TANKS; i++)
        {
            int health_bar_start_x = i * (HEALTH_BAR_WIDTH + HEALTH_BAR_SPACING) + HEALTH_BARS_OFFSET_X;
            int health_bar_start_y = (t < 1) ? 0 : (SCRHEIGHT - HEALTH_BAR_HEIGHT) - 1;
            int health_bar_end_x = health_bar_start_x + HEALTH_BAR_WIDTH;
            int health_bar_end_y = (t < 1) ? HEALTH_BAR_HEIGHT : SCRHEIGHT - 1;

            screen->bar(health_bar_start_x, health_bar_start_y, health_bar_end_x, health_bar_end_y, REDMASK);
            screen->bar(health_bar_start_x, health_bar_start_y + (int)((double)HEALTH_BAR_HEIGHT * (1 - (sorted_health.at(i) / (double)TANK_MAX_HEALTH))), health_bar_end_x, health_bar_end_y, GREENMASK);
        }
    }
}
// -----------------------------------------------------------
// Sort tanks by health value using count sort
// -----------------------------------------------------------
std::vector<int> Game::count_sort(std::vector<Tank*>& tank_pointers, int begin, int num_tanks)
{
    std::vector<int> temp(num_tanks, 0);
    int end = begin + num_tanks;
    const int max = TANK_MAX_HEALTH + 1;
    int count[max];
    
    for (int i = 0; i < max; ++i)
    {
	    count[i] = 0;
    }
    
    // Calculate count of elements
    for (int i = 0; i < num_tanks; i++)
    {
	    count[tank_pointers.at(begin + i)->health]++;
    }
    
    //Calculate cummulative count
    for (int i = 1; i < max; i++)
    {
	    count[i] += count[i - 1];
    }
    
    // Place the elements in sorted order
    for (int i = num_tanks-1; i >= 0; i--)
    {
        temp.at(count[tank_pointers.at(begin + i)->health] - 1) = tank_pointers.at(begin + i)->health;
        count[tank_pointers.at(begin + i)->health]--;
    }
    
    return temp;
}

// -----------------------------------------------------------
// When we reach MAX_FRAMES print the duration and speedup multiplier
// Updating REF_PERFORMANCE at the top of this file with the value
// on your machine gives you an idea of the speedup your optimizations give
// -----------------------------------------------------------
void Tmpl8::Game::measure_performance()
{
    char buffer[128];
    if (frame_count >= MAX_FRAMES)
    {
        if (!lock_update)
        {
            duration = perf_timer.elapsed();
            cout << "Duration was: " << duration << " (Replace REF_PERFORMANCE with this value)" << endl;
            lock_update = true;
        }

        frame_count--;
    }

    if (lock_update)
    {
        screen->bar(420, 170, 870, 430, 0x030000);
        int ms = (int)duration % 1000, sec = ((int)duration / 1000) % 60, min = ((int)duration / 60000);
        sprintf(buffer, "%02i:%02i:%03i", min, sec, ms);
        frame_count_font->centre(screen, buffer, 200);
        sprintf(buffer, "SPEEDUP: %4.1f", REF_PERFORMANCE / duration);
        frame_count_font->centre(screen, buffer, 340);
    }
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Game::tick(float deltaTime)
{
    if (!lock_update)
    {
        update(deltaTime);
    }
    draw();

    measure_performance();

    // print something in the graphics window
    //screen->Print("hello world", 2, 2, 0xffffff);

    // print something to the text window
    //cout << "This goes to the console window." << std::endl;

    //Print frame count
    frame_count++;
    string frame_count_string = "FRAME: " + std::to_string(frame_count);
    frame_count_font->print(screen, frame_count_string.c_str(), 350, 580);
}
