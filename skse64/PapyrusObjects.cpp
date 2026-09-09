#include "PapyrusObjects.h"

#include "Serialization.h"

#include "PapyrusVM.h"
#include <algorithm>

namespace
{
	static const size_t kMaxNameLen  = 1024;
	bool StackLookupProbeEnabled()
	{
		char flag[2] = {};
		return GetEnvironmentVariableA("SKSE_AUTOMATION_STACK_LOOKUP_PROBE", flag, sizeof(flag)) == 1 && flag[0] == '1';
	}
}

///
/// SKSEObjectRegistry
///

void SKSEObjectRegistry::RegisterFactory(ISKSEObjectFactory * factory)
{
	uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(factory);
	std::string className( factory->ClassName() );
	factoryMap_[className] = vtbl;
}

const ISKSEObjectFactory* SKSEObjectRegistry::GetFactoryByName(const char* name) const
{
	std::string t( name );

	const ISKSEObjectFactory* result = NULL;

	FactoryMapT::const_iterator it = factoryMap_.find(t);
	if (it != factoryMap_.end())
	{
		result = reinterpret_cast<const ISKSEObjectFactory*>(&it->second);
	}

	return result;
}

///
/// SKSEPersistentObjectStorage
///

void SKSEPersistentObjectStorage::CleanDroppedStacks()
{
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();

	// Explicit automation-only coverage of the active-stack lookup. An empty
	// persistent-object table otherwise skips the path which crashed on save.
	// Read an existing engine stack; do not create stacks or alter save payloads.
	if (StackLookupProbeEnabled())
	{
		UInt32 probeId = 0;
		void* expected = nullptr;
		registry->stackLock.Lock();
		UInt64 legacyValue = 0;
		auto choose = [&](VMStackTableItem* item) {
			if (!item->data) return true;
			UInt32 embeddedId = 0;
			memcpy(&embeddedId, reinterpret_cast<const char*>(item->data) + 0x80, sizeof(embeddedId));
			if (embeddedId != item->stackId) return true;
			probeId = item->stackId;
			expected = item->data;
			memcpy(&legacyValue, reinterpret_cast<const char*>(expected) + 0x18, sizeof(legacyValue));
			return false;
		};
		registry->allStacks.ForEach(choose);
		registry->stackLock.Release();
		if (expected) {
			_MESSAGE("STACK_LOOKUP_PROBE_BEGIN id=%u expected=%p legacy_first_value=%016llX persistent_slots=%u", probeId, expected, legacyValue, static_cast<UInt32>(data_.size()));
			// Logging is outside the spinlock. Revalidate after that gap, then
			// retain the recursive SimpleLock across the actual lookup and ID
			// check. A departed/replaced stack is not successful coverage.
			VMStackInfo* actual = nullptr;
			UInt32 embeddedId = 0;
			bool covered = false;
			registry->stackLock.Lock();
			auto* current = registry->allStacks.Find(&probeId);
			if (current && current->data == expected) {
				memcpy(&embeddedId, reinterpret_cast<const char*>(current->data) + 0x80, sizeof(embeddedId));
				if (embeddedId == probeId) {
					actual = registry->GetStackInfo(probeId);
					// Never dereference the returned opaque pointer, especially
					// after unlocking. Read the map-owned Stack while protected.
					memcpy(&embeddedId, reinterpret_cast<const char*>(current->data) + 0x80, sizeof(embeddedId));
					covered = true;
				}
			}
			registry->stackLock.Release();
			if (covered) {
				_MESSAGE("STACK_LOOKUP_PROBE_END id=%u actual=%p expected=%p embedded_id=%u match=%u", probeId, actual, expected, embeddedId, static_cast<UInt32>(actual == expected && embeddedId == probeId));
			} else {
				_MESSAGE("STACK_LOOKUP_PROBE_NOT_COVERED id=%u: stack gone or replaced during logging gap", probeId);
			}
		} else {
			_MESSAGE("STACK_LOOKUP_PROBE_NOT_COVERED: no verified active stack; not a passing coverage test");
		}
	}

	for (UInt32 i=0; i<data_.size(); i++)
	{
		Entry& e = data_[i];

		if (e.obj == NULL)
			continue;

		if (registry->GetStackInfo(e.owningStackId) != NULL)
			continue;

		// Stack no longer active, drop this entry

		delete e.obj;
		e.obj = NULL;

		freeIndices_.push_back(i);

		_MESSAGE("SKSEPersistentObjectStorage::CleanDroppedStacks: Freed object at index %d.", i);
	}
}

void SKSEPersistentObjectStorage::ClearAndRelease()
{
	freeIndices_.clear();

	for (DataT::iterator it = data_.begin(); it != data_.end(); ++it)
	{
		Entry& e = *it;
		if (e.obj != NULL)
			delete e.obj;
	}

	data_.clear();
}

bool SKSEPersistentObjectStorage::Save(SKSESerializationInterface* intfc)
{
	using namespace Serialization;

	// Before saving, purge entries whose owning stack is no longer running.
	// This can happen if someone forgot to release an object.
	// We don't want these resource leaks to pile up in the co-save.
	CleanDroppedStacks();

	// Save data
	UInt32 dataSize = data_.size();
	if (! WriteData(intfc, &dataSize))
		return false;

	UInt32 filledSize = data_.size() - freeIndices_.size();
	if (StackLookupProbeEnabled())
		_MESSAGE("STACK_STORAGE_SAVE slots=%u filled=%u", dataSize, filledSize);
	if (! WriteData(intfc, &filledSize))
		return false;

	for (UInt32 i=0; i<dataSize; i++)
	{
		Entry& e = data_[i];

		// Data of free indices is null, so we skip these
		if (e.obj == NULL)
			continue;
		
		// Skip to next entry if write failed
		if (! WriteSKSEObject(intfc, e.obj))
			continue;

		WriteData(intfc, &e.owningStackId);

		UInt32 index = i;
		WriteData(intfc, &index);
	}

	return true;
}

bool SKSEPersistentObjectStorage::Load(SKSESerializationInterface* intfc, UInt32 loadedVersion)
{
	using namespace Serialization;

	// Load data
	UInt32 dataSize;
	if (! ReadData(intfc,&dataSize))
		return false;

	data_.resize(dataSize);

	UInt32 filledSize;
	if (! ReadData(intfc,&filledSize))
		return false;

	for (UInt32 i=0; i<filledSize; i++)
	{
		Entry e = { 0 };

		if (! ReadSKSEObject(intfc, e.obj))
			continue;

		ReadData(intfc, &e.owningStackId);

		UInt32 index;
		ReadData(intfc, &index);

		data_[index] = e;
	}
	
	// Rebuild free index list
	for (UInt32 i=0; i<data_.size(); i++)
		if (data_[i].obj == NULL)
			freeIndices_.push_back(i);
	if (StackLookupProbeEnabled())
		_MESSAGE("STACK_STORAGE_LOAD slots=%u filled=%u restored=%u", dataSize, filledSize, static_cast<UInt32>(data_.size() - freeIndices_.size()));

	return true;
}

SInt32 SKSEPersistentObjectStorage::Store(ISKSEObject* obj, UInt32 owningStackId)
{
	IScopedCriticalSection scopedLock( &lock_ );

	Entry e = { obj, owningStackId };

	SInt32 index;

	if (freeIndices_.empty())
	{
		index = data_.size();
		data_.push_back(e);
	}
	else
	{
		index = freeIndices_.back();
		freeIndices_.pop_back();
		data_[index] = e;
	}

	return index + 1;
}

ISKSEObject* SKSEPersistentObjectStorage::Access(SInt32 handle)
{
	IScopedCriticalSection scopedLock( &lock_ );

	SInt32 index = handle - 1;

	if (index < 0 || index >= data_.size())
	{
		_MESSAGE("SKSEPersistentObjectStorage::AccessObject(%d): Invalid handle.", handle);
		return NULL;
	}

	Entry& e = data_[index];

	if (e.obj == NULL)
	{
		_MESSAGE("SKSEPersistentObjectStorage::AccessObject(%d): Object was NULL.", handle);
		return NULL;
	}

	ISKSEObject* result = e.obj;
	if (result == NULL)
	{
		_MESSAGE("SKSEPersistentObjectStorage::AccessObject(%d): Invalid type (%s).", handle, e.obj->ClassName());
		return NULL;
	}

	return result;
}

ISKSEObject* SKSEPersistentObjectStorage::Take(SInt32 handle)
{
	IScopedCriticalSection scopedLock( &lock_ );

	SInt32 index = handle - 1;

	if (index < 0 || index >= data_.size())
	{
		_MESSAGE("SKSEPersistentObjectStorage::AccessObject(%d): Invalid handle.", handle);
		return NULL;
	}

	Entry& e = data_[index];

	if (e.obj == NULL)
	{
		_MESSAGE("SKSEPersistentObjectStorage::TakeObject(%d): Object was NULL.", handle);
		return NULL;
	}

	ISKSEObject* result = e.obj;
	if (result != NULL)
	{
		e.obj = NULL;
		freeIndices_.push_back(index);
	}
	else
	{
		_MESSAGE("SKSEPersistentObjectStorage::TakeObject(%d): Invalid type (%s).", handle, e.obj->ClassName());
		return NULL;
	}

	return result;
}

///
/// Serialization helpers
///

bool WriteSKSEObject(SKSESerializationInterface* intfc, ISKSEObject* obj)
{
	using namespace Serialization;

	const char*  name	 = obj->ClassName();
	const UInt32 version = obj->ClassVersion();

	intfc->OpenRecord('OBJE', version);

	size_t rawLen = strlen(name);
	UInt32 len    = (std::min)(rawLen, kMaxNameLen);

	if (! WriteData(intfc, &len))
		return false;

	if (! intfc->WriteRecordData(name, len))
		return false;

	return obj->Save(intfc);
}

bool ReadSKSEObject(SKSESerializationInterface* intfc, ISKSEObject*& objOut)
{
	UInt32 type, length, objVersion;

	if (! intfc->GetNextRecordInfo(&type, &objVersion, &length))
		return false;

	if (type != 'OBJE')
	{
		_MESSAGE("ReadSKSEObject: Error loading unexpected chunk type %08X (%.4s)", type, &type);
		return false;
	}

	// Read the name of the serialized class
	UInt32 len;
	if (! intfc->ReadRecordData(&len, sizeof(len)))
		return false;

	if (len > kMaxNameLen)
	{
		_MESSAGE("ReadSKSEObject: Serialization error. Class name len extended kMaxNameLen.");
		return false;
	}

	char buf[kMaxNameLen+1] = { 0 };
	if (! intfc->ReadRecordData(&buf, len))
		return false;

	// Get the factory
	const ISKSEObjectFactory* factory = SKSEObjectRegistryInstance().GetFactoryByName(buf);
	if (factory == NULL)
	{
		_MESSAGE("ReadSKSEObject: Serialization error. Factory missing for %s.", &buf);
		return false;
	}

	// Intantiate and load the actual data
	ISKSEObject* obj = factory->Create();
	if (! obj->Load(intfc, objVersion))
	{
		// Load failed. clean up.
		objOut = NULL;
		delete obj;

		return false;
	}

	objOut = obj;
	return true;
}

///
/// Global instances
///

SKSEObjectRegistry& SKSEObjectRegistryInstance()
{
	static SKSEObjectRegistry instance;
	return instance;
}

SKSEPersistentObjectStorage& SKSEObjectStorageInstance()
{
	static SKSEPersistentObjectStorage instance;
	return instance;
}
