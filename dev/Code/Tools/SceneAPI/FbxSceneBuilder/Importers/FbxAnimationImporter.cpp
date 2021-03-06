/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/Debug/TraceContext.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <SceneAPI/FbxSceneBuilder/FbxSceneSystem.h>
#include <SceneAPI/FbxSceneBuilder/Importers/FbxAnimationImporter.h>
#include <SceneAPI/FbxSceneBuilder/Importers/FbxImporterUtilities.h>
#include <SceneAPI/FbxSceneBuilder/Importers/FbxTransformImporter.h>
#include <SceneAPI/FbxSceneBuilder/Importers/Utilities/RenamedNodesMap.h>
#include <SceneAPI/FbxSDKWrapper/FbxNodeWrapper.h>
#include <SceneAPI/FbxSDKWrapper/FbxSceneWrapper.h>
#include <SceneAPI/FbxSDKWrapper/FbxTimeSpanWrapper.h>

#include <SceneAPI/SceneCore/Containers/Scene.h>
#include <SceneAPI/SceneCore/Utilities/Reporting.h>
#include <SceneAPI/SceneData/GraphData/BoneData.h>
#include <SceneAPI/SceneData/GraphData/AnimationData.h>

#include <SceneAPI/FbxSDKWrapper/FbxAnimLayerWrapper.h>
#include <SceneAPI/FbxSDKWrapper/FbxAnimCurveNodeWrapper.h>
#include <SceneAPI/FbxSDKWrapper/FbxAnimCurveWrapper.h>

namespace AZ
{
    namespace SceneAPI
    {
        namespace FbxSceneBuilder
        {
            const char* FbxAnimationImporter::s_animationNodeName = "animation";
            const FbxSDKWrapper::FbxTimeWrapper::TimeMode FbxAnimationImporter::s_defaultTimeMode = 
                FbxSDKWrapper::FbxTimeWrapper::frames30;

            FbxAnimationImporter::FbxAnimationImporter()
            {
                BindToCall(&FbxAnimationImporter::ImportAnimation);
            }

            void FbxAnimationImporter::Reflect(ReflectContext* context)
            {
                SerializeContext* serializeContext = azrtti_cast<SerializeContext*>(context);
                if (serializeContext)
                {
                    serializeContext->Class<FbxAnimationImporter, SceneCore::LoadingComponent>()->Version(1);
                }
            }

            Events::ProcessingResult FbxAnimationImporter::ImportAnimation(SceneNodeAppendedContext& context)
            {
                AZ_TraceContext("Importer", "Animation");

                // Add check for animation layers at the scene level.

                if (context.m_sourceScene.GetAnimationStackCount() <= 0)
                {
                    return Events::ProcessingResult::Ignored;
                }

                if (context.m_sourceNode.IsMesh())
                {
                    return ImportBlendShapeAnimation(context);
                }

                if (!context.m_sourceNode.IsBone())
                {
                    return Events::ProcessingResult::Ignored;
                }

                AZStd::string nodeName = s_animationNodeName;
                RenamedNodesMap::SanitizeNodeName(nodeName, context.m_scene.GetGraph(), context.m_currentGraphPosition);
                AZ_TraceContext("Animation node name", nodeName);

                AZStd::shared_ptr<SceneData::GraphData::AnimationData> createdAnimationData = 
                    AZStd::make_shared<SceneData::GraphData::AnimationData>();

                auto animStackWrapper = context.m_sourceScene.GetAnimationStackAt(0);
                const FbxSDKWrapper::FbxTimeSpanWrapper timeSpan = animStackWrapper->GetLocalTimeSpan();
                const double frameRate = timeSpan.GetFrameRate();

                if (frameRate == 0.0)
                {
                    AZ_TracePrintf(Utilities::ErrorWindow, "Scene has a 0 framerate. Animation cannot be processed without timing information.");
                    return Events::ProcessingResult::Failure;
                }

                const double startTime = timeSpan.GetStartTime();
                const double stopTime = timeSpan.GetStopTime();
                const double sampleTimeStep = 1.0 / frameRate;

                const size_t frameCount = static_cast<size_t>(ceil((stopTime - startTime) / sampleTimeStep));

                createdAnimationData->ReserveKeyFrames(frameCount);
                createdAnimationData->SetTimeStepBetweenFrames(sampleTimeStep);

                for (double time = startTime; time <= stopTime; time += sampleTimeStep)
                {
                    FbxSDKWrapper::FbxTimeWrapper frameTime;
                    frameTime.SetTime(time);

                    Transform animTransform = context.m_sourceNode.EvaluateLocalTransform(frameTime);
                    context.m_sourceSceneSystem.SwapTransformForUpAxis(animTransform);
                    context.m_sourceSceneSystem.ConvertBoneUnit(animTransform);

                    createdAnimationData->AddKeyFrame(animTransform);
                }

                Containers::SceneGraph::NodeIndex addNode = context.m_scene.GetGraph().AddChild(
                    context.m_currentGraphPosition, nodeName.c_str(), AZStd::move(createdAnimationData));
                context.m_scene.GetGraph().MakeEndPoint(addNode);

                return Events::ProcessingResult::Success;
            }

            Events::ProcessingResult FbxAnimationImporter::ImportBlendShapeAnimation(SceneNodeAppendedContext& context)
            {
                FbxNode * node = context.m_sourceNode.GetFbxNode();
                FbxMesh * pMesh = node->GetMesh();
                if (!pMesh)
                {
                    return Events::ProcessingResult::Ignored;
                }

                int deformerCount = pMesh->GetDeformerCount(FbxDeformer::eBlendShape);
                int blendShapeIndex = -1;
                AZStd::string nodeName;
                AZStd::string animNodeName;
                for (int deformerIndex = 0; deformerIndex < deformerCount; ++deformerIndex)
                {
                    //we are assuming 1 anim stack (single animation clip export)
                    const FbxBlendShape* pDeformer = (FbxBlendShape*)pMesh->GetDeformer(deformerIndex, FbxDeformer::eBlendShape);
                    if (!pDeformer)
                    {
                        continue;
                    }
                    blendShapeIndex++;
                    int blendShapeChannelCount = pDeformer->GetBlendShapeChannelCount();
                    int stackCount = context.m_sourceScene.GetAnimationStackCount();
                    auto animStackWrapper = context.m_sourceScene.GetAnimationStackAt(0);

                    const FbxSDKWrapper::FbxTimeSpanWrapper timeSpan = animStackWrapper->GetLocalTimeSpan();
                    const double frameRate = timeSpan.GetFrameRate();

                    if (frameRate == 0.0)
                    {
                        AZ_TracePrintf("Animation_Warning", "Scene has a 0 framerate. Animation cannot be processed without timing information.");
                        return Events::ProcessingResult::Failure;
                    }

                    const double startTime = timeSpan.GetStartTime();
                    const double stopTime = timeSpan.GetStopTime();
                    const double sampleTimeStep = 1.0 / frameRate;

                    const size_t frameCount = static_cast<size_t>(ceil((stopTime - startTime) / sampleTimeStep));


                    const int layerCount = animStackWrapper->GetAnimationLayerCount();

                    for (int blendShapeChannelIdx = 0; blendShapeChannelIdx < blendShapeChannelCount; ++blendShapeChannelIdx)
                    {
                        const FbxBlendShapeChannel* pChannel = pDeformer->GetBlendShapeChannel(blendShapeChannelIdx);
                        if (!pChannel)
                        {
                            continue;
                        }

                        for (int layerIndex = 0; layerIndex < layerCount; layerIndex++)
                        {
                            FbxAnimLayer* animationLayer = animStackWrapper->GetAnimationLayerAt(layerIndex)->GetFbxLayer();
                            FbxAnimCurve* animCurve = pMesh->GetShapeChannel(blendShapeIndex, blendShapeChannelIdx, animationLayer);
                            if (!animCurve)
                            {
                                continue;
                            }
                            AZStd::shared_ptr<FbxSDKWrapper::FbxAnimCurveWrapper> animCurveWrapper = AZStd::make_shared<FbxSDKWrapper::FbxAnimCurveWrapper>(animCurve);

                            AZStd::shared_ptr<SceneData::GraphData::BlendShapeAnimationData> createdAnimationData =
                                AZStd::make_shared<SceneData::GraphData::BlendShapeAnimationData>();

                            createdAnimationData->ReserveKeyFrames(frameCount);
                            createdAnimationData->SetTimeStepBetweenFrames(sampleTimeStep);

                            for (double time = startTime; time <= stopTime; time += sampleTimeStep)
                            {
                                FbxSDKWrapper::FbxTimeWrapper frameTime;
                                frameTime.SetTime(time);

                                //weight values from FBX are range 0 - 100
                                float sampleValue = animCurveWrapper->Evaluate(frameTime)/ 100.0f;
                                createdAnimationData->AddKeyFrame(sampleValue);
                            }

                            nodeName = pChannel->GetName();
                            const size_t dotIndex = nodeName.find_last_of('.');
                            nodeName = nodeName.substr(dotIndex + 1);
                            createdAnimationData->SetBlendShapeName(nodeName.c_str());
                            animNodeName = AZStd::string::format("%s_%s", s_animationNodeName, nodeName.c_str());
                            Containers::SceneGraph::NodeIndex addNode = context.m_scene.GetGraph().AddChild(
                                context.m_currentGraphPosition, animNodeName.c_str(), AZStd::move(createdAnimationData));
                            context.m_scene.GetGraph().MakeEndPoint(addNode);
                        }
                    }
                }
                return Events::ProcessingResult::Success;
            }
        } // namespace FbxSceneBuilder
    } // namespace SceneAPI
} // namespace AZ
