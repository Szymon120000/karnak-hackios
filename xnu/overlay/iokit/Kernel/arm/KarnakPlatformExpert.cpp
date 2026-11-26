/*
 * Minimal device-tree platform expert for karnak (MediaTek MT8163).
 *
 * The kernel receives the stock MediaTek/LK device tree, not Apple's.  It has
 * no /arm-io, /options or NVRAM node, so this platform expert only publishes
 * the real CPU and device nodes; the GIC and architected timer are driven
 * from the verified constants in KARNAK.h instead of DT lookups.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pexpert/arm64/board_config.h>

#if defined(KARNAK)
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>

class KarnakPlatformExpert : public IODTPlatformExpert {
	OSDeclareDefaultStructors(KarnakPlatformExpert);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void processTopLevel(IORegistryEntry *root) APPLE_KEXT_OVERRIDE;
	const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	bool getTargetName(char *name, int maxLength) APPLE_KEXT_OVERRIDE;
	bool getProductName(char *name, int maxLength) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(KarnakPlatformExpert, IODTPlatformExpert);

bool
KarnakPlatformExpert::start(IOService *provider)
{
	IOLog("KARNAK: starting device-tree platform expert\n");
	if (!IODTPlatformExpert::start(provider)) {
		return false;
	}
	registerService();
	IOLog("KARNAK: platform expert registered\n");
	return true;
}

const char *
KarnakPlatformExpert::deleteList(void)
{
	return "()";
}

const char *
KarnakPlatformExpert::excludeList(void)
{
	return "('chosen', 'defaults', 'cpus', 'options')";
}

void
KarnakPlatformExpert::processTopLevel(IORegistryEntry *root)
{
	/*
	 * The stock implementation requires an Apple NVRAM /options node.
	 * karnak has none; publish the CPU and device nodes without inventing
	 * one.  createNubs() adds the topology's logical CPU IDs.
	 */
	IORegistryEntry *cpus = root->childFromPath("cpus", gIODTPlane);
	if (cpus != NULL) {
		createNubs(this, IODTFindMatchingEntries(cpus, kIODTExclusive, NULL));
		cpus->release();
	}
	createNubs(this, IODTFindMatchingEntries(root, kIODTExclusive, excludeList()));
}

bool
KarnakPlatformExpert::getTargetName(char *name, int maxLength)
{
	if (name == NULL || maxLength <= 0) {
		return false;
	}
	strlcpy(name, "karnak", (size_t)maxLength);
	return true;
}

bool
KarnakPlatformExpert::getProductName(char *name, int maxLength)
{
	if (name == NULL || maxLength <= 0) {
		return false;
	}
	strlcpy(name, "Amazon Fire HD 8 (2018)", (size_t)maxLength);
	return true;
}
#endif /* KARNAK */
