#ifndef ROR_OGRE_NEXT_NAMESPACE_REMAP_H
#define ROR_OGRE_NEXT_NAMESPACE_REMAP_H

// This header is force-included only while compiling the private embedded
// OgreNext fork and translation units that consume that fork. It must never be
// an INTERFACE compile option: OGRE14 translation units retain namespace Ogre.
#define ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP 1
#define Ogre RoROgreNext

// Dynamic plugin entry points have C linkage and therefore do not inherit the
// C++ namespace remap. Keep them private to the embedded fork as well.
#define dllStartPlugin RoROgreNext_dllStartPlugin
#define dllStopPlugin RoROgreNext_dllStopPlugin
#define ROR_OGRE_NEXT_DLL_START_PLUGIN_SYMBOL \
    "RoROgreNext_dllStartPlugin"
#define ROR_OGRE_NEXT_DLL_STOP_PLUGIN_SYMBOL \
    "RoROgreNext_dllStopPlugin"

// Objective-C class names live in one process-global runtime registry. Prefix
// every class implemented by OgreNext's macOS/iOS render and sample targets,
// including names which do not begin with "Ogre" upstream.
#define OgreConfigWindowDelegate RoROgreNextConfigWindowDelegate
#define OgreMetalView RoROgreNextMetalView
#define MetalWinListener RoROgreNextMetalWinListener
#define CocoaWindowDelegate RoROgreNextCocoaWindowDelegate
#define OgreGL3PlusView RoROgreNextGL3PlusView
#define OgreGL3PlusWindow RoROgreNextGL3PlusWindow
#define EAGL2View RoROgreNextEAGL2View
#define EAGL2ViewController RoROgreNextEAGL2ViewController
#define OgreView RoROgreNextView
#define OgreWindow RoROgreNextWindow
#define AppDelegate RoROgreNextAppDelegate
#define GameViewController RoROgreNextGameViewController
#define RestartViewController RoROgreNextRestartViewController
#define TutorialViewController RoROgreNextTutorialViewController

#endif
