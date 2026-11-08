#include "SettingsPage.h"

namespace ff
{
SettingsPage::SettingsPage (EngineHost& h, ProjectDocument& d, juce::PropertiesFile& s)
    : host (h), doc (d), settings (s),
      selector (h.deviceManager, 0, 32, 0, 32, true, true, false, false)
{
    selector.setSize (560, 600);
    selectorViewport.setViewedComponent (&selector, false);
    selectorViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (selectorViewport);

    autosaveLabel.setText ("Autosave", juce::dontSendNotification);
    backupsLabel.setText ("Backups kept per project", juce::dontSendNotification);
    logLabel.setText ("Logs: " + juce::FileLogger::getSystemLogFileFolder().getChildFile ("RAVENYR").getFullPathName(),
                      juce::dontSendNotification);
    for (auto* l : { &autosaveLabel, &backupsLabel, &logLabel })
    {
        styleLabel (*l, 13.0f);
        addAndMakeVisible (l);
    }

    const std::pair<int, const char*> opts[] = { { 0, "Off" }, { 30, "Every 30 seconds" }, { 60, "Every minute" },
                                                 { 120, "Every 2 minutes" }, { 300, "Every 5 minutes" }, { 600, "Every 10 minutes" } };
    for (auto& [secs, name] : opts)
        autosaveCombo.addItem (name, secs + 1);
    autosaveCombo.setSelectedId (doc.getAutosaveIntervalSeconds() + 1, juce::dontSendNotification);
    autosaveCombo.onChange = [this] { doc.setAutosaveIntervalSeconds (autosaveCombo.getSelectedId() - 1); };
    addAndMakeVisible (autosaveCombo);

    backupsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    backupsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
    backupsSlider.setRange (0, 50, 1);
    backupsSlider.setValue (settings.getIntValue ("backupsToKeep", 10), juce::dontSendNotification);
    backupsSlider.onValueChange = [this] { settings.setValue ("backupsToKeep", (int) backupsSlider.getValue()); };
    addAndMakeVisible (backupsSlider);

    revealLogs.onClick = [] {
        auto dir = juce::FileLogger::getSystemLogFileFolder().getChildFile ("RAVENYR");
        dir.createDirectory();
        dir.revealToUser();
    };
    addAndMakeVisible (revealLogs);

    for (auto* b : { &generalTab, &pluginsTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, palette::accent);
        b->setColour (juce::TextButton::textColourOnId, palette::onAccent);
        addAndMakeVisible (b);
    }
    generalTab.onClick = [this] { showTab (0); };
    pluginsTab.onClick = [this] { showTab (1); };
    showTab (0);
}

void SettingsPage::setPluginPanel (std::unique_ptr<juce::Component> p)
{
    pluginPanel = std::move (p);
    if (pluginPanel != nullptr) addChildComponent (*pluginPanel);
    showTab (tab);
}

void SettingsPage::showTab (int t)
{
    tab = t;
    generalTab.setToggleState (tab == 0, juce::dontSendNotification);
    pluginsTab.setToggleState (tab == 1, juce::dontSendNotification);
    for (juce::Component* c : { (juce::Component*) &selectorViewport, (juce::Component*) &autosaveLabel, (juce::Component*) &backupsLabel,
                                (juce::Component*) &logLabel, (juce::Component*) &autosaveCombo, (juce::Component*) &backupsSlider,
                                (juce::Component*) &revealLogs })
        c->setVisible (tab == 0);
    if (pluginPanel != nullptr) pluginPanel->setVisible (tab == 1);
    resized();
    repaint();
}

void SettingsPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    if (welcome)
    {
        g.setColour (palette::accentDim.withAlpha (0.35f));
        g.fillRoundedRectangle (welcomeBox.toFloat(), 6.0f);
        g.setColour (palette::accent);
        g.setFont (uiFont (16.0f, true));
        auto w = welcomeBox.reduced (16, 10);
        g.drawText ("Welcome to RAVENYR", w.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (palette::text);
        g.setFont (uiFont (13.5f));
        g.drawFittedText ("Pick your audio interface, enable the input your guitar is plugged into, and choose a buffer size "
                          "(128-256 samples is a good start). Then click the tuner display at the top and play a string: the input meter "
                          "and tuner confirm your signal is arriving.", w, juce::Justification::topLeft, 3);
    }
    if (tab == 0)
    {
        drawSectionPanel (g, audioBox, "Audio & MIDI devices");
        drawSectionPanel (g, appBox, "Projects");
    }
    else if (pluginPanel != nullptr)
        drawSectionPanel (g, pluginPanel->getBounds().expanded (8).withTrimmedTop (-24), "Plugins: folders to scan, your plugins, disabled plugins");
}

void SettingsPage::resized()
{
    auto r = getLocalBounds().reduced (16);
    if (welcome)
    {
        welcomeBox = r.removeFromTop (84);
        r.removeFromTop (12);
    }
    auto tabs = r.removeFromTop (30);
    generalTab.setBounds (tabs.removeFromLeft (150));
    tabs.removeFromLeft (6);
    pluginsTab.setBounds (tabs.removeFromLeft (110));
    r.removeFromTop (12);
    if (pluginPanel != nullptr) pluginPanel->setBounds (r.reduced (8).withTrimmedTop (24));
    audioBox = r.removeFromLeft (juce::jmin (640, r.getWidth() * 3 / 5));
    r.removeFromLeft (16);
    appBox = r.removeFromTop (270);

    auto a = audioBox.reduced (8).withTrimmedTop (24);
    selectorViewport.setBounds (a);
    selector.setSize (a.getWidth() - selectorViewport.getScrollBarThickness(), juce::jmax (a.getHeight(), 560));

    auto p = appBox.reduced (14).withTrimmedTop (22);
    autosaveLabel.setBounds (p.removeFromTop (20));
    autosaveCombo.setBounds (p.removeFromTop (28).withWidth (240));
    p.removeFromTop (10);
    backupsLabel.setBounds (p.removeFromTop (20));
    backupsSlider.setBounds (p.removeFromTop (28).withWidth (300));
    p.removeFromTop (10);
    logLabel.setBounds (p.removeFromTop (36));
    revealLogs.setBounds (p.removeFromTop (28).withWidth (160));
}
} // namespace ff
