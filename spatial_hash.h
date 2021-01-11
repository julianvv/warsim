#pragma once
#include <vector>
#include "tank.h"

typedef std::vector<Tank*> spatial_cell;


class spatial_hash
{
public:
	unsigned int height;
	unsigned int width;
	int cell_size;
	unsigned int cell_amount;
	int x_start;
	int y_start;
	std::vector<spatial_cell> cells;
	
	spatial_hash(unsigned int height, unsigned int width, int cell_size, int x, int y);

	void add_tank(Tank* tank);
	void move_tank(Tank* tank, float x, float y);
	void remove_tank(Tank* tank);
	spatial_cell* position_to_cell(float x, float y);
	int spatial_hash::get_cell_size();
};

