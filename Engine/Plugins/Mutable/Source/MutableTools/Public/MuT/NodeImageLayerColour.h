// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"

namespace mu
{

	// Forward definitions
	class NodeColour;
	typedef Ptr<NodeColour> NodeColourPtr;
	typedef Ptr<const NodeColour> NodeColourPtrConst;

	class NodeImageLayerColour;
	typedef Ptr<NodeImageLayerColour> NodeImageLayerColourPtr;
	typedef Ptr<const NodeImageLayerColour> NodeImageLayerColourPtrConst;


	//! This node applies a layer blending effect on a base image using a mask and a colour.
	//! \ingroup model
	class MUTABLETOOLS_API NodeImageLayerColour : public NodeImage
	{
	public:

		NodeImageLayerColour();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the node generating the base image that will have the blending effect applied.
		NodeImagePtr GetBase() const;
		void SetBase( NodeImagePtr );

		//! Get the node generating the mask image controlling the weight of the effect.
		NodeImagePtr GetMask() const;
		void SetMask( NodeImagePtr );

		//! Get the node generating the color to blend on the base.
		NodeColourPtr GetColour() const;
		void SetColour( NodeColourPtr );

		//!
		EBlendType GetBlendType() const;

		//!
		void SetBlendType(EBlendType);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageLayerColour();

	private:

		Private* m_pD;

	};


}
