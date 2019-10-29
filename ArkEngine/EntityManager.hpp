#pragma once

#include "Component.hpp"
#include "Script.hpp"
#include "Entity.hpp"

#include <libs/static_any.hpp>

#include <unordered_map>
#include <typeindex>
#include <optional>
#include <bitset>
#include <tuple>
#include <set>

class EntityManager final : public NonCopyable, public NonMovable {

public:
	EntityManager(ComponentManager& cm, ScriptManager& sm): componentManager(cm), scriptManager(sm) {}
	~EntityManager() = default;

public:

	Entity createEntity(std::string name)
	{
		Entity e;
		e.manager = this;
		if (!freeEntities.empty()) {
			e.id = freeEntities.back();
			freeEntities.pop_back();
			auto& entity = getEntity(e);
		} else {
			e.id = entities.size();
			auto& entity = entities.emplace_back();
			entity.mask.reset();
		}
		auto& entity = getEntity(e);

		if (name.empty())
			entity.name = std::string("entity_") + std::to_string(e.id);
		else
			entity.name = name;

		EngineLog(LogSource::EntityM, LogLevel::Info, "created (%s)", entity.name.c_str());
		return e;
	}

	// TODO (entity manager): figure out a way to copy components without templates
	// only components are copied, scripts are excluded, maybe it's possible in ArchetypeManager?
	Entity cloneEntity(Entity e)
	{
		//Entity hClone = createEntity();
		//auto entity = getEntity(e);
		//auto clone = getEntity(hClone);
	}

	const std::string& getNameOfEntity(Entity e)
	{
		auto& entity = getEntity(e);
		return entity.name;
	}

	void setNameOfEntity(Entity e, std::string name)
	{
		auto& entity = getEntity(e);
		entity.name = name;
	}

	Entity getEntityByName(std::string name)
	{
		for (int i = 0; i < entities.size(); i++) {
			if (entities[i].name == name) {
				Entity e;
				e.id = i;
				e.manager = this;
				return e;
			}
		}
		EngineLog(LogSource::EntityM, LogLevel::Warning, "getByName(): didn't find entity (%s)", name.c_str());
		return {};
	}

	void destroyEntity(Entity e)
	{
		freeEntities.push_back(e.id);
		auto& entity = getEntity(e);

		EngineLog(LogSource::EntityM, LogLevel::Info, "destroyed entity (%s)", entity.name.c_str());

		for (auto compData : entity.components)
			componentManager.removeComponent(compData.id, compData.index);

		scriptManager.removeScripts(entity.scriptsIndex);

		//entity.childrenIndex = ArkInvalidIndex;
		entity.name.clear();
		entity.mask.reset();
		entity.scriptsIndex = ArkInvalidIndex;
		entity.components.clear();
	}

	// if component already exists, then the existing component is returned
	template <typename T, typename... Args>
	T& addComponentOnEntity(Entity e, Args&&... args)
	{
		int compId = componentManager.getComponentId<T>();
		auto& entity = getEntity(e);
		if (entity.mask.test(compId))
			return *getComponentOfEntity<T>(e);

		auto component = addComponentOnEntity(e, typeid(T));
		Util::construct_in_place<T>(component, std::forward<Args>(args)...);
		return *reinterpret_cast<T*>(component);
	}

	void* addComponentOnEntity(Entity e, std::type_index type)
	{
		int compId = componentManager.getComponentId(type);
		auto& entity = getEntity(e);
		if (entity.mask.test(compId))
			return getComponentOfEntity(e, type);
		markAsDirty(e);
		entity.mask.set(compId);

		auto [comp, compIndex] = componentManager.addComponent(compId);
		auto& compData = entity.components.emplace_back();
		compData.component = comp;
		compData.id = compId;
		compData.index = compIndex;
		return comp;
	}

	void* getComponentOfEntity(Entity e, std::type_index type)
	{
		auto& entity = getEntity(e);
		int compId = componentManager.getComponentId(type);
		if (!entity.mask.test(compId)) {
			EngineLog(LogSource::EntityM, LogLevel::Warning, "entity (%s), doesn't have component (%s)", entity.name.c_str(), Util::getNameOfType(type));
			return nullptr;
		}
		return entity.getComponent(compId);
	}

	template <typename T>
	T* getComponentOfEntity(Entity e)
	{
		return reinterpret_cast<T*>(getComponentOfEntity(e, typeid(T)));
	}

	void removeComponentOfEntity(Entity e, std::type_index type)
	{
		auto& entity = getEntity(e);
		auto compId = componentManager.getComponentId(type);
		if (entity.mask.test(compId)) {
			markAsDirty(e);
			componentManager.removeComponent(compId, entity.getComponentIndex(compId));
			entity.mask.set(compId, false);
			Util::erase_if(entity.components, [compId](const auto& compData) { return compData.id == compId; });
		}
	}

	template <typename T, typename... Args>
	T* addScriptOnEntity(Entity e, Args&&... args)
	{
		auto& entity = getEntity(e);
		auto script = scriptManager.addScript<T>(entity.scriptsIndex, std::forward<Args>(args)...);
		script->m_entity = e;
		return script;
	}

	template <typename T>
	T* getScriptOfEntity(Entity e)
	{
		auto& entity = getEntity(e);
		auto script = scriptManager.getScript<T>(entity.scriptsIndex);
		if (script == nullptr)
			EngineLog(LogSource::EntityM, LogLevel::Warning, "entity (%s) doesn't have (%s) script attached", entity.name.c_str(), Util::getNameOfType<T>());
		return script;
	}

	template <typename T>
	void removeScriptOfEntity(Entity e)
	{
		auto& entity = getEntity(e);
		scriptManager.removeScript<T>(entity.scriptsIndex);
	}

	template <typename T>
	void setScriptActive(Entity e, bool active)
	{
		auto& entity = getEntity(e);
		scriptManager.setActive<T>(entity.scriptsIndex, active);
	}

	const ComponentManager::ComponentMask& getComponentMaskOfEntity(Entity e) {
		auto& entity = getEntity(e);
		return entity.mask;
	}

	void markAsDirty(Entity e)
	{
		dirtyEntities.insert(e);
	}

	std::optional<std::set<Entity>> getDirtyEntities()
	{
		if (dirtyEntities.empty())
			return std::nullopt;
		else
			return std::move(dirtyEntities);
	}

#if 0 // disable entity children
	void addChildTo(Entity p, Entity c)
	{
		if (!isValidEntity(p))
			return;
		if (!isValidEntity(c)) {
			std::cerr << "above entity is an invalid child";
			return;
		}

		getEntity(c).parentIndex = p.id;
		auto parent = getEntity(p);
		if (p.hasChildren()) {
			childrenTree.at(parent.childrenIndex).push_back(c);
		} else {
			childrenTree.emplace_back();
			parent.childrenIndex = childrenTree.size() - 1;
			childrenTree.back().push_back(c);
		}
	}

	std::optional<Entity> getParentOfEntity(Entity e)
	{
		auto entity = getEntity(e);
		if (entity.parentIndex == ArkInvalidIndex)
			return {};
		else {
			Entity parent(*this);
			parent.id = entity.parentIndex;
			return parent;
		}
	}

	std::vector<Entity> getChildrenOfEntity(Entity e, int depth)
	{
		if (!isValidEntity(e))
			return {};

		auto entity = getEntity(e);
		if (entity.childrenIndex == ArkInvalidIndex || depth == 0)
			return {};

		auto cs = childrenTree.at(entity.childrenIndex);

		if (depth == 1)
			return cs;

		std::vector<Entity> children(cs);
		for (auto child : cs) {
			if (depth == Entity::AllChilren) {
				auto grandsons = getChildrenOfEntity(child, depth); // maintain depth
				children.insert(children.end(), grandsons.begin(), grandsons.end());
			} else {
				auto grandsons = getChildrenOfEntity(child, depth - 1); // 
				children.insert(children.end(), grandsons.begin(), grandsons.end());
			}
		}
		return children;
			
	}

	bool entityHasChildren(Entity e)
	{
		return getEntity(e).childrenIndex == ArkInvalidIndex;
	}
#endif // disable entity children

private:

	struct InternalEntityData {
		struct ComponentData {
			using Self = const ComponentData&;
			int16_t id;
			int16_t index;
			void* component; // reference
			friend bool operator==(Self left, Self right) { return left.id == right.id; }
		};
		//int16_t childrenIndex = ArkInvalidIndex;
		//int16_t parentIndex = ArkInvalidIndex;
		ComponentManager::ComponentMask mask;
		std::vector<ComponentData> components;
		int16_t scriptsIndex = ArkInvalidIndex;
		std::string name = "";

		void* getComponent(int id)
		{
			auto compData = std::find_if(std::begin(components), std::end(components), [=](const auto& compData) { return compData.id == id; });
			return compData->component;
		}

		template <typename T> 
		T* getComponent(int id) { 
			return reinterpret_cast<T*>(getComponent(id)); 
		}

		int16_t getComponentIndex(int id)
		{
			auto compData = std::find_if(std::begin(components), std::end(components), [=](const auto& compData) { return compData.id == id; });
			return compData->index;
		}
	};

	InternalEntityData& getEntity(Entity e)
	{
		return entities.at(e.id);
	}

private:
	std::vector<InternalEntityData> entities;
	std::set<Entity> dirtyEntities;
	std::vector<int> freeEntities;
	ComponentManager& componentManager;
	ScriptManager& scriptManager;
	//std::vector<std::vector<Entity>> childrenTree;
};

template<typename T, typename ...Args>
inline T& Entity::addComponent(Args&& ...args)
{
	static_assert(std::is_base_of_v<Component<T>, T>, " T is not a Component");
	return manager->addComponentOnEntity<T>(*this, std::forward<Args>(args)...);
}

template<typename T>
inline T& Entity::getComponent()
{
	static_assert(std::is_base_of_v<Component<T>, T>, " T is not a Component");
	auto comp = manager->getComponentOfEntity<T>(*this);
	if (!comp)
		EngineLog(LogSource::EntityM, LogLevel::Error, ">:( \n going to crash..."); // going to crash...
	return *comp;
}

inline void Entity::addComponent(std::type_index type)
{
	manager->addComponentOnEntity(*this, type);
}

template<typename T>
inline T* Entity::tryGetComponent()
{
	static_assert(std::is_base_of_v<Component<T>, T>, " T is not a Component");
	return manager->getComponentOfEntity<T>(*this);
}

template <typename T>
inline void Entity::removeComponent()
{
	static_assert(std::is_base_of_v<Component<T>, T>, " T is not a Component");
	manager->removeComponentOfEntity(*this, typeid(T));
}

inline void Entity::removeComponent(std::type_index type)
{
	manager->removeComponentOfEntity(*this, type);
}

template<typename T, typename ...Args>
inline T* Entity::addScript(Args&& ...args)
{
	static_assert(std::is_base_of_v<Script, T>, " T is not a Script");
	return manager->addScriptOnEntity<T>(*this, std::forward<Args>(args)...);
}

template<typename T>
inline T* Entity::getScript()
{
	static_assert(std::is_base_of_v<Script, T>, " T is not a Script");
	return manager->getScriptOfEntity<T>(*this);
}

template<typename T>
inline void Entity::setScriptActive(bool active)
{
	static_assert(std::is_base_of_v<Script, T>, " T is not a Script");
	return manager->setScriptActive<T>(*this, active);
}

template<typename T>
inline void Entity::removeScript()
{
	static_assert(std::is_base_of_v<Script, T>, " T is not a Script");
	return manager->removeScriptOfEntity<T>(*this);
}




