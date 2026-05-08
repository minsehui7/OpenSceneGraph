/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/
#include <osgUtil/RenderLeaf>
#include <osgUtil/StateGraph>
#include <osg/Notify>

#include <cstdlib>
#include <map>
#include <string>

using namespace osg;
using namespace osgUtil;

namespace {
bool hlTerrainClassRenderLeafDebugEnabled() {
    static int s_cached = -1;
    if (s_cached < 0) {
        const char *e = ::getenv("HL_TERRAIN_CLASS_RENDERLEAF_DEBUG");
        s_cached = (e && e[0] != '\0' && e[0] != '0' && e[0] != 'f' && e[0] != 'F' && e[0] != 'n' && e[0] != 'N') ? 1 : 0;
    }
    return s_cached != 0;
}
} // namespace

void RenderLeaf::render(osg::RenderInfo& renderInfo,RenderLeaf* previous)
{
    osg::State& state = *renderInfo.getState();
    static unsigned int sShadowBinProbeCount = 0u;
    static unsigned int sProbeLastFrame = 0u;
    static std::map<const void*, unsigned int> sDrawableHitsInFrame;
    static unsigned int sCountClear = 0u;
    static unsigned int sCountShadow = 0u;
    static unsigned int sCountFill = 0u;
    static unsigned int sCountOutline = 0u;
    if (renderInfo.getView() && renderInfo.getView()->getFrameStamp())
    {
        const unsigned int fn = renderInfo.getView()->getFrameStamp()->getFrameNumber();
        if (fn != sProbeLastFrame)
        {
            if (hlTerrainClassRenderLeafDebugEnabled() && sProbeLastFrame != 0u &&
                (sCountClear || sCountShadow || sCountFill || sCountOutline))
            {
                OSG_NOTICE << "[HLDBG] terrain-class frame " << sProbeLastFrame
                           << " totals: clear=" << sCountClear
                           << " shadow=" << sCountShadow
                           << " fill=" << sCountFill
                           << " outline=" << sCountOutline
                           << std::endl;
            }
            sProbeLastFrame = fn;
            sShadowBinProbeCount = 0u;
            sDrawableHitsInFrame.clear();
            sCountClear = sCountShadow = sCountFill = sCountOutline = 0u;
        }
    }
    // ShapefileTerrainClassification uses binBase = 272000 + renderBin*20 + passIndex*10 (+0 stencil / +1 shadow / +2 fill / +3 lines).
    static constexpr int kHlTerrainClassBinLo = 272000;
    static constexpr int kHlTerrainClassBinHi = 285000;

    auto logShadowBinProbe = [&](StateGraph* rg) {
        if (!hlTerrainClassRenderLeafDebugEnabled())
            return;
        if (!rg || !_drawable.valid())
            return;
        const osg::StateSet* ss = rg->getStateSet();
        if (!ss)
            return;
        const int binNum = ss->getBinNumber();
        if (binNum < kHlTerrainClassBinLo || binNum >= kHlTerrainClassBinHi)
            return;
        if (sShadowBinProbeCount >= 128u)
            return;

        ++sShadowBinProbeCount;
        const osg::Object* obj = dynamic_cast<const osg::Object*>(_drawable.get());
        const osg::Drawable* dr = dynamic_cast<const osg::Drawable*>(_drawable.get());
        const char* name = (obj && !obj->getName().empty()) ? obj->getName().c_str() : "<unnamed>";
        const std::string drawableName = (obj && !obj->getName().empty()) ? obj->getName() : std::string();
        unsigned int& hitCount = sDrawableHitsInFrame[_drawable.get()];
        ++hitCount;
        if (hitCount > 1u)
        {
            OSG_NOTICE << "[HLDBG] DUP-RENDERLEAF same drawable in frame="
                       << sProbeLastFrame
                       << " bin=" << binNum
                       << " drawable=" << _drawable.get()
                       << " name=" << name
                       << " hit=" << hitCount
                       << std::endl;
        }
        if (drawableName.find("ClassificationClearStencil") != std::string::npos) ++sCountClear;
        else if (drawableName.find("ClassificationShadowVolume") != std::string::npos) ++sCountShadow;
        else if (drawableName.find("ClassificationFillFSQ") != std::string::npos ||
                 drawableName.find("ClassificationColorFullscreen") != std::string::npos) ++sCountFill;
        else if (drawableName.find("ClassificationOutline") != std::string::npos) ++sCountOutline;
        OSG_NOTICE << "[HLDBG] terrain-class-bin draw #" << sShadowBinProbeCount
                   << " bin=" << binNum
                   << " drawable=" << _drawable.get()
                   << " name=" << name
                   << " cb=" << (dr ? dr->getDrawCallback() : 0)
                   << " depth=" << _depth
                   << std::endl;
    };

    // don't draw this leaf if the abort rendering flag has been set.
    if (state.getAbortRendering())
    {
        //cout << "early abort"<<endl;
        return;
    }

    if (previous)
    {

        // apply matrices if required.
        state.applyProjectionMatrix(_projection.get());
        state.applyModelViewMatrix(_modelview.get());

        // apply state if required.
        StateGraph* prev_rg = previous->_parent;
        StateGraph* prev_rg_parent = prev_rg->_parent;
        StateGraph* rg = _parent;
        if (prev_rg_parent!=rg->_parent)
        {
            StateGraph::moveStateGraph(state,prev_rg_parent,rg->_parent);

            // send state changes and matrix changes to OpenGL.
            state.apply(rg->getStateSet());
            logShadowBinProbe(rg);

        }
        else if (rg!=prev_rg)
        {

            // send state changes and matrix changes to OpenGL.
            state.apply(rg->getStateSet());
            logShadowBinProbe(rg);

        }

        // if we are using osg::Program which requires OSG's generated uniforms to track
        // modelview and projection matrices then apply them now.
        if (state.getUseModelViewAndProjectionUniforms()) state.applyModelViewAndProjectionUniformsIfRequired();

        // draw the drawable
        _drawable->draw(renderInfo);
    }
    else
    {
        // apply matrices if required.
        state.applyProjectionMatrix(_projection.get());
        state.applyModelViewMatrix(_modelview.get());

        // apply state if required.
        StateGraph::moveStateGraph(state,NULL,_parent->_parent);

        state.apply(_parent->getStateSet());
        logShadowBinProbe(_parent);

        // if we are using osg::Program which requires OSG's generated uniforms to track
        // modelview and projection matrices then apply them now.
        if (state.getUseModelViewAndProjectionUniforms()) state.applyModelViewAndProjectionUniformsIfRequired();

        // draw the drawable
        _drawable->draw(renderInfo);
    }

    if (_dynamic)
    {
        state.decrementDynamicObjectCount();
    }

    // OSG_NOTICE<<"RenderLeaf "<<_drawable->getName()<<" "<<_depth<<std::endl;
}
