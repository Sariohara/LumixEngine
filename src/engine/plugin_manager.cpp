#include "engine/array.h"
#include "engine/debug.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix 
{


class PluginManagerImpl final : public PluginManager
{
	private:
		typedef Array<IPlugin*> PluginList;
		typedef Array<void*> LibraryList;


	public:
		PluginManagerImpl(Engine& engine, IAllocator& allocator)
			: m_plugins(allocator)
			, m_libraries(allocator)
			, m_allocator(allocator)
			, m_engine(engine)
			, m_library_loaded(allocator)
		{ }


		~PluginManagerImpl()
		{
			for (int i = m_plugins.size() - 1; i >= 0; --i)
			{
				LUMIX_DELETE(m_engine.getAllocator(), m_plugins[i]);
			}

			for (void* lib : m_libraries)
			{
				OS::unloadLibrary(lib);
			}
		}


		void initPlugins() override
		{
			PROFILE_FUNCTION();
			for (int i = 0, c = m_plugins.size(); i < c; ++i)
			{
				m_plugins[i]->init();
			}
		}


		void update(float dt, bool paused) override
		{
			PROFILE_FUNCTION();
			for (int i = 0, c = m_plugins.size(); i < c; ++i)
			{
				m_plugins[i]->update(dt);
			}
		}


		void* getLibrary(IPlugin* plugin) const override
		{
			int idx = m_plugins.indexOf(plugin);
			if (idx < 0) return nullptr;

			return m_libraries[idx];
		}


		const Array<void*>& getLibraries() const override
		{
			return m_libraries;
		}


		const Array<IPlugin*>& getPlugins() const override
		{
			return m_plugins;
		}


		IPlugin* getPlugin(const char* name) override
		{
			for (IPlugin* plugin : m_plugins)
			{
				if (equalStrings(plugin->getName(), name))
				{
					return plugin;
				}
			}
			return nullptr;
		}


		DelegateList<void(void*)>& libraryLoaded() override
		{
			return m_library_loaded;
		}
		

		void unload(IPlugin* plugin) override
		{
			int idx = m_plugins.indexOf(plugin);
			ASSERT(idx >= 0);
			LUMIX_DELETE(m_engine.getAllocator(), m_plugins[idx]);
			OS::unloadLibrary(m_libraries[idx]);
			m_libraries.erase(idx);
			m_plugins.erase(idx);
		}


		IPlugin* load(const char* path) override
		{
			char path_with_ext[MAX_PATH_LENGTH];
			copyString(path_with_ext, path);
			const char* ext =
			#ifdef _WIN32
				".dll";
			#elif defined __linux__
				".so";
			#else 
				#error Unknown platform
			#endif
			if (!PathUtils::hasExtension(path, ext + 1)) catString(path_with_ext, ext);
			logInfo("Core") << "loading plugin " << path_with_ext;
			typedef IPlugin* (*PluginCreator)(Engine&);
			auto* lib = OS::loadLibrary(path_with_ext);
			if (lib)
			{
				PluginCreator creator = (PluginCreator)OS::getLibrarySymbol(lib, "createPlugin");
				if (creator)
				{
					IPlugin* plugin = creator(m_engine);
					if (!plugin)
					{
						logError("Core") << "createPlugin failed.";
						LUMIX_DELETE(m_engine.getAllocator(), plugin);
						ASSERT(false);
					}
					else
					{
						addPlugin(plugin);
						m_libraries.push(lib);
						m_library_loaded.invoke(lib);
						logInfo("Core") << "Plugin loaded.";
						Debug::StackTree::refreshModuleList();
						return plugin;
					}
				}
				else
				{
					logError("Core") << "No createPlugin function in plugin.";
				}
				OS::unloadLibrary(lib);
			}
			else
			{
				auto* plugin = StaticPluginRegister::create(path, m_engine);
				if (plugin)
				{
					logInfo("Core") << "Plugin loaded.";
					addPlugin(plugin);
					return plugin;
				}
				logWarning("Core") << "Failed to load plugin.";
			}
			return nullptr;
		}


		IAllocator& getAllocator() { return m_allocator; }


		void addPlugin(IPlugin* plugin) override
		{
			m_plugins.push(plugin);
			for (auto* i : m_plugins)
			{
				i->pluginAdded(*plugin);
				plugin->pluginAdded(*i);
			}
		}


	private:
		Engine& m_engine;
		DelegateList<void(void*)> m_library_loaded;
		LibraryList m_libraries;
		PluginList m_plugins;
		IAllocator& m_allocator;
};
	

PluginManager* PluginManager::create(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), PluginManagerImpl)(engine, engine.getAllocator());
}


void PluginManager::destroy(PluginManager* manager)
{
	LUMIX_DELETE(static_cast<PluginManagerImpl*>(manager)->getAllocator(), manager);
}


} // namespace Lumix
