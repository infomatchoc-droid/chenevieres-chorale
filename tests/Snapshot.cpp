// Capture de l'interface : ChoraleSnapshot sortie.png [size=N] [preset=N] [select=S] [scale=1.5] [play]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ParamIDs.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_LINUX
 #include <dlfcn.h>
 static int ignoreXError (void*, void*) { return 0; }
 static void tolerateXErrors()
 {
     if (auto* lib = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL))
         if (auto fn = (void* (*) (int (*) (void*, void*))) dlsym (lib, "XSetErrorHandler"))
             fn (ignoreXError);
 }
#else
 static void tolerateXErrors() {}
#endif

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Desktop::getInstance();
    tolerateXErrors();
    if (argc < 2) { std::printf ("usage : ChoraleSnapshot sortie.png [options]\n"); return 1; }
    juce::StringArray opts;
    for (int i = 2; i < argc; ++i) opts.add (argv[i]);
    auto value = [&] (const char* key, const juce::String& def) -> juce::String
    {
        for (auto& o : opts) if (o.startsWith (juce::String (key) + "=")) return o.fromFirstOccurrenceOf ("=", false, false);
        return def;
    };

    std::unique_ptr<ChoraleProcessor> p (static_cast<ChoraleProcessor*> (createPluginFilter()));
    const double fs = 48000.0;
    p->setPlayConfigDetails (2, 2, fs, 512);
    p->prepareToPlay (fs, 512);
    if (opts.contains ("play") || true)
    {
        // un peu de signal pour la note détectée et le vumètre
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        double ph = 0;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < 512; ++i)
            {
                ph += 220.0 / fs;
                const float x = (float) (0.3 * std::sin (2 * juce::MathConstants<double>::pi * ph) + 0.15 * std::sin (4 * juce::MathConstants<double>::pi * ph));
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            p->processBlock (b, m);
        }
    }
    const int preset = value ("preset", "-1").getIntValue();
    if (preset >= 0) p->setCurrentProgram (preset);
    if (value ("size", "").isNotEmpty())
        if (auto* par = p->apvts.getParameter (ids::size)) par->setValueNotifyingHost (par->convertTo0to1 ((float) value ("size", "5").getIntValue()));

    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    const float scale = value ("scale", "1").getFloatValue();
    ed->setSize ((int) (ChoraleEditor::baseW * scale), (int) (ChoraleEditor::baseH * scale));
    ed->addToDesktop (0);
    ed->setVisible (true);
    if (auto* ce = dynamic_cast<ChoraleEditor*> (ed.get())) juce::ignoreUnused (ce);
    // laisser les minuteries mettre à jour la note et la scène, puis capturer
    struct Shot : juce::Timer
    {
        juce::AudioProcessorEditor* ed; juce::File out; int frame = 0;
        void timerCallback() override
        {
            if (++frame < 12) return;
            stopTimer();
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat png;
            png.writeImageToStream (img, os);
            std::printf ("écrit : %s (%d × %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    } shot;
    shot.ed = ed.get();
    shot.out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    shot.startTimer (40);
    juce::MessageManager::getInstance()->runDispatchLoop();
    ed->removeFromDesktop();
    ed.reset();
    return 0;
}
