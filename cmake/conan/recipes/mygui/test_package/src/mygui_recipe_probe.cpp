#include <MyGUI_LogManager.h>
#include <MyGUI_OgreDataManager.h>
#include <MyGUI_ResourceTrueTypeFont.h>
#include <OgreStringConverter.h>
#include <OgreVector.h>

#include <string>

namespace
{
void exercise_true_type_font_runtime()
{
    MyGUI::ResourceTrueTypeFont true_type_font;
    (void)true_type_font.getDefaultHeight();
}
} // namespace

int main(int argc, char** argv)
{
    // These constructors safely exercise one out-of-line symbol from each
    // MyGUI archive without creating an OGRE renderer or native window.
    MyGUI::LogManager log_manager;
    log_manager.setSTDOutputEnabled(false);
    MyGUI::OgreDataManager data_manager;

    // Keep a runtime-reachable ResourceTrueTypeFont construction path so its
    // complete archive member (and direct FreeType references) must link. The
    // normal smoke run avoids initialising a font without a RenderManager.
    if (argc == 2 &&
        std::string(argv[1]) == "--exercise-font-runtime")
    {
        exercise_true_type_font_runtime();
    }

    const Ogre::Vector3 vector(1.0f, 2.0f, 2.0f);
    const std::string serialized =
        Ogre::StringConverter::toString(vector);
    return data_manager.getGroup().empty() && !serialized.empty()
               ? 0
               : 1;
}
