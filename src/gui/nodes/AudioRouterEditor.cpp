/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "engine/nodes/AudioRouterNode.h"
#include "gui/nodes/AudioRouterEditor.h"
#include "gui/LookAndFeel.h"
#include "gui/Artist.h"
#include "Common.h"

namespace Element {

class AudioRouterMatrix : public kv::PatchMatrixComponent
{
public:
    AudioRouterMatrix (AudioRouterEditor& ed)
        : editor (ed)
    {
        setMatrixCellSize (48);
        setSize (getRowThickness() * 4, 
                 getColumnThickness() * 4);
        setRepaintsOnMouseActivity (true);
    }

    int getNumColumns() override    { return editor.getMatrixState().getNumColumns(); }
    int getNumRows() override       { return editor.getMatrixState().getNumRows(); }

    void paintMatrixCell (Graphics& g, const int width, const int height,
                                       const int row, const int column) override
    {
        auto& matrix = editor.getMatrixState();
        const int gridPadding = 1;
        bool useHighlighting = true;

        if (useHighlighting &&
                (mouseIsOverCell (row, column) && ! matrix.connected (row, column)))
        {
            g.setColour (Colors::elemental.withAlpha (0.4f));
            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }
        else if ((mouseIsOverRow(row) || mouseIsOverColumn(column)) && !matrix.connected (row, column))
        {
            g.setColour (Colors::elemental.withAlpha (0.3f));
            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }
        else
        {
            g.setColour (matrix.connected (row, column) ?
                            Colour (kv::Colors::elemental.brighter()) :
                            Colour (kv::LookAndFeel_KV1::defaultMatrixCellOffColor));
    
            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }
    }

    void matrixCellClicked (const int row, const int col, const MouseEvent& ev) override
    {
        ignoreUnused (ev);
        auto& matrix = editor.getMatrixState();
        matrix.toggleCell (row, col);
        editor.applyMatrix();
        repaint();
    }

    void matrixBackgroundClicked (const MouseEvent& ev) override { }

    void matrixHoveredCellChanged (const int prevRow, const int prevCol,
                                   const int newRow,  const int newCol) override
    {
        ignoreUnused (prevRow, prevCol, newRow, newCol);
        repaint();
    }

private:
    AudioRouterEditor& editor;
};


class AudioRouterSizeButton : public TextButton
{
public:
    AudioRouterSizeButton (AudioRouterEditor& o)
        : owner (o)
    {
        stabilizeContent();

        onClick = [this]() {
            PopupMenu menu;
            menu.addItem (2, "2x2", true, false);
            menu.addItem (4, "4x4", true, false);
            menu.addItem (8, "8x8", true, false);
            menu.addItem (10, "10x10", true, false);
            menu.addItem (10, "12x12", true, false);
            menu.addItem (16, "16x16", true, false);
            menu.showMenuAsync (PopupMenu::Options()
                    .withTargetComponent (this),
                ModalCallbackFunction::create (sizeChosen, WeakReference<AudioRouterSizeButton> (this)));
        };
    }

    ~AudioRouterSizeButton()
    {
        masterReference.clear();
    }

    std::function<void(int)> onAudioRouterSizeChanged;

    void stabilizeContent()
    {
        setButtonText ("Size");
        // setButtonText (owner.getSizeString());
    }

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE(AudioRouterSizeButton)
    
    AudioRouterEditor& owner;
    void handleSizeResult (int r)
    {
        // owner.setRouterSize (r, r);
        if (r > 1 && onAudioRouterSizeChanged)
            onAudioRouterSizeChanged (r);
        stabilizeContent();
    }

    static void sizeChosen (int code, WeakReference<AudioRouterSizeButton> me)
    {
        if (me != nullptr)
            me->handleSizeResult (code);
    }
};

class AudioRouterEditor::Content : public Component
{
public:
    Content (AudioRouterEditor& o)
        : owner (o)
    {
        setOpaque (true);
        matrix.reset (new AudioRouterMatrix (o));
        addAndMakeVisible (matrix.get());

        sizeButton.reset (new AudioRouterSizeButton (o));
        addAndMakeVisible (sizeButton.get());
        sizeButton->onAudioRouterSizeChanged = [this](int size)
        {
            if (auto* node = owner.getNodeObjectOfType<AudioRouterNode>())
            {
                node->setSize (size, size);
                if (owner.autoResize)
                {
                    // matrix needs to be correct before adjusting overall size
                    owner.matrix = node->getMatrixState();
                    owner.adjustBoundsToMatrixSize (32);
                }
            }
        };

        // addAndMakeVisible (slider);
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 1, 1);
        slider.setRange (0.001, 2.0);

        slider.onValueChange = [this] { owner.setFadeLength (slider.getValue()); };

        setSize (padding + labelWidth + matrix->getWidth(), 
                 padding + labelWidth + matrix->getHeight());
        matrixArea = { labelWidth, padding, matrix->getWidth(), matrix->getHeight() };
    }

    ~Content()
    {
        slider.onValueChange = nullptr;
    }

    void adjustBoundsToMatrixSize (int cellSize = 0)
    {
        cellSize = cellSize > 0 ? cellSize : 36;
        matrix->setMatrixCellSize (cellSize, cellSize);
        
        setSize (padding + labelWidth + (matrix->getNumColumns() * matrix->getColumnThickness()), 
                 padding + labelWidth +  (matrix->getNumRows() * matrix->getRowThickness()));
    }

    void resized() override
    {
        auto size = jlimit (24, 36, 
            roundToInt ((double)(getWidth() - labelWidth - 32) / (double)matrix->getNumRows()));
        matrix->setMatrixCellSize (size, size);

        matrixArea = { labelWidth, padding, 
                matrix->getRowThickness() * matrix->getNumRows(), 
                matrix->getColumnThickness() * matrix->getNumColumns() };

        matrix->setBounds (matrixArea);
        if (slider.isVisible())
            slider.setBounds (matrixArea.getX() - size + 2, matrixArea.getBottom() + 4, size - 2, size - 2);

        auto r1 = getLocalBounds();
        int btnH = 24;
        int btnW = 36;
        sizeButton->changeWidthToFitText (btnH);
        sizeButton->setBounds ((labelWidth / 2) - (btnW / 2),
                               matrixArea.getBottom() + (labelWidth / 2) - (btnH / 2),
                               btnW, btnH);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (LookAndFeel::contentBackgroundColor);
        Rectangle<int> box (0, padding, labelWidth - padding, matrix->getHeight());
        auto rowThickness = matrix->getRowThickness();
        auto colThickness = matrix->getColumnThickness();

        g.setColour (LookAndFeel::textColor);
        for (int row = 0; row < owner.getMatrixState().getNumRows(); ++row)
            g.drawText (String("Ch. ") + String(row + 1), box.removeFromTop(rowThickness),
                Justification::centredRight, false);

        box = { matrix->getX(), matrix->getBottom() + 10, matrix->getWidth(), 50 };
        
        for (int col = 0; col < owner.getMatrixState().getNumColumns(); ++col)
        {
            auto r  = box.removeFromLeft (colThickness);
            g.setColour(LookAndFeel::textColor);
            Artist::drawVerticalText (g, String("Ch. ") + String(col + 1), r,
                                         Justification::centredRight);
        }
    }

private:
    friend class AudioRouterEditor;
    AudioRouterEditor& owner;
    int padding = 10;
    int labelWidth = 60;
    Rectangle<int> matrixArea;
    Slider slider;
    std::unique_ptr<AudioRouterSizeButton> sizeButton;
    std::unique_ptr<AudioRouterMatrix> matrix;
};

AudioRouterEditor::AudioRouterEditor (const Node& node)
    : NodeEditorComponent (node)
{
    setOpaque (true);
    content.reset (new Content (*this));
    addAndMakeVisible (content.get());
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        changeListenerCallback (node); // initial gui state
        node->addChangeListener (this);
    }

    adjustBoundsToMatrixSize (32);
}

AudioRouterEditor::~AudioRouterEditor()
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->removeChangeListener (this);
    content.reset();
}

void AudioRouterEditor::setFadeLength (double length)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->setFadeLength (length);
}

void AudioRouterEditor::applyMatrix()
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->setMatrixState (matrix);
}

String AudioRouterEditor::getSizeString() const
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        return node->getSizeString();
    return {};
}

void AudioRouterEditor::changeListenerCallback (ChangeBroadcaster*)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        matrix = node->getMatrixState();
        content->resized();
        content->repaint();
        content->matrix->repaint();
        content->sizeButton->stabilizeContent();
        resized();
    }
}

void AudioRouterEditor::adjustBoundsToMatrixSize (int cellSize)
{
    content->adjustBoundsToMatrixSize (cellSize);
    setSize (content->getWidth(), content->getHeight());
}

void AudioRouterEditor::resized()
{
    content->setBounds (getLocalBounds());
}

void AudioRouterEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);
}

}
