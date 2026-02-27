////////////////////////////////////////////////////////////////////////////
//	Module 		: moving_objects.cpp
//	Created 	: 27.03.2007
//  Modified 	: 27.03.2007
//	Author		: Dmitriy Iassenev
//	Description : moving objects
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "moving_objects.h"
#include "ai_space.h"
#include "level_graph.h"
#include "moving_object.h"

moving_objects::moving_objects() :
	m_tree(0)
{
}

moving_objects::~moving_objects()
{
	xr_delete(m_tree);
}

void moving_objects::on_level_load()
{
	xr_delete(m_tree);
	m_tree = xr_new<TREE>(ai().level_graph().header().box(), ai().level_graph().header().cell_size() * .5f, 16 * 1024,
	                      16 * 1024);

	if (!m_pending_registration.empty())
	{
		NEAREST_MOVING::const_iterator I = m_pending_registration.begin();
		NEAREST_MOVING::const_iterator E = m_pending_registration.end();
		for (; I != E; ++I)
			m_tree->insert(*I);
		m_pending_registration.clear_not_free();
	}
}

void moving_objects::ensure_tree_initialized()
{
	if (m_tree)
		return;

	if (!ai().get_level_graph())
		return;

	on_level_load();
}

void moving_objects::register_object(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) == m_objects.end(),
		make_string("moving object %s is registers twice",*moving_object->id())
	);

	m_objects.insert		(moving_object);
#endif // DEBUG

	ensure_tree_initialized();
	if (!m_tree)
	{
		m_pending_registration.push_back(moving_object);
		return;
	}

	m_tree->insert(moving_object);
}

void moving_objects::unregister_object(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) != m_objects.end(),
		make_string("moving object %s is not yet registered or unregisters twice",*moving_object->id())
	);

	m_objects.erase			(m_objects.find(moving_object));
#endif // DEBUG

	if (!m_tree)
	{
		NEAREST_MOVING::iterator it = std::find(m_pending_registration.begin(), m_pending_registration.end(), moving_object);
		if (it != m_pending_registration.end())
			m_pending_registration.erase(it);
		return;
	}

	m_tree->remove(moving_object);
}

void moving_objects::on_object_move(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) != m_objects.end(),
		make_string("moving object %s is not yet registered",*moving_object->id())
	);
#endif
#pragma todo("this place can be optimized in case of slowdowns")
	if (!m_tree)
		return;

	m_tree->remove(moving_object);

	moving_object->update_position();

	m_tree->insert(moving_object);
}

void moving_objects::clear()
{
	m_pending_registration.clear_not_free();
	m_previous_collisions.clear_not_free();
}
