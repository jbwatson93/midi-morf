#pragma once

#include <JuceHeader.h>

class PageContainer : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pageChanged (int newPageIndex) = 0;
    };

    PageContainer() {}

    void setListener (Listener* l) { listener = l; }

    void addPage (juce::Component* page)
    {
        pages.add (page);
        addChildComponent (page);

        if (pages.size() == 1)
        {
            page->setVisible (true);
            currentPage = 0;
        }
    }

    int getCurrentPage() const { return currentPage; }
    int getNumPages() const { return pages.size(); }

    void setPage (int index)
    {
        if (index < 0 || index >= pages.size() || index == currentPage)
            return;

        pages[currentPage]->setVisible (false);
        currentPage = index;
        pages[currentPage]->setVisible (true);
        pages[currentPage]->setBounds (getLocalBounds());

        if (listener != nullptr)
            listener->pageChanged (currentPage);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        for (int i = 0; i < pages.size(); ++i)
            pages[i]->setBounds (area);
    }

private:
    juce::Array<juce::Component*> pages;
    int currentPage = 0;
    Listener* listener = nullptr;
};
