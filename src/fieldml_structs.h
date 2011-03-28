/* \file
 * $Id$
 * \author Caton Little
 * \brief 
 *
 * \section LICENSE
 *
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is FieldML
 *
 * The Initial Developer of the Original Code is Auckland Uniservices Ltd,
 * Auckland, New Zealand. Portions created by the Initial Developer are
 * Copyright (C) 2010 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 */

#ifndef H_FIELDML_STRUCTS
#define H_FIELDML_STRUCTS

#include <vector>
#include <string>

#include "fieldml_api.h"
#include "SimpleMap.h"
#include "SimpleBitset.h"

extern const int INVALID_LOCATION_HANDLE;
extern const int LOCAL_LOCATION_HANDLE;
extern const int LIBRARY_LOCATION_HANDLE;

class FieldmlObject
{
public:
    const FieldmlHandleType type;
    const std::string name;
    const bool isVirtual;
    int locationHandle;

    int intValue;
    
    FieldmlObject( const std::string _name, int _locationHandle, FieldmlHandleType _type, bool _isVirtual );
    
    virtual ~FieldmlObject();
};


class EnsembleType :
    public FieldmlObject
{
public:
    const bool isComponentEnsemble;

    SimpleBitset members;
    
    EnsembleType( const std::string _name, int _locationHandle, bool _isComponentEnsemble, bool _isVirtual );
};


class ElementSequence :
    public FieldmlObject
{
public:
    const FmlObjectHandle elementType;

    SimpleBitset members;
    
    ElementSequence( const std::string _name, int _locationHandle, FmlObjectHandle _componentType );
};


class ContinuousType :
    public FieldmlObject
{
public:
    const FmlObjectHandle componentType;
    
    ContinuousType( const std::string _name, int _locationHandle, FmlObjectHandle _componentType, bool _isVirtual );
};


class MeshType :
    public FieldmlObject
{
public:
    const FmlObjectHandle xiType;
    const FmlObjectHandle elementType;
    
    SimpleMap<int, std::string> shapes;
    
    MeshType( const std::string _name, int _region, FmlObjectHandle _xiType, FmlObjectHandle _elementType, bool _isVirtual );
};


class Evaluator :
    public FieldmlObject
{
public:
    const FmlObjectHandle valueType;

    std::vector<FmlObjectHandle> variables;

    Evaluator( const std::string _name, int _region, FieldmlHandleType _type, FmlObjectHandle _valueType, bool _isVirtual );
};


class ReferenceEvaluator :
    public Evaluator
{
public:
    const FmlObjectHandle sourceEvaluator;

    SimpleMap<FmlObjectHandle, FmlObjectHandle> binds;

    ReferenceEvaluator( const std::string _name, int _region, FmlObjectHandle _evaluator, FmlObjectHandle _valueType, bool _isVirtual );
};


class PiecewiseEvaluator :
    public Evaluator
{
public:
    FmlObjectHandle indexEvaluator;
    
    SimpleMap<FmlObjectHandle, FmlObjectHandle> binds;
    SimpleMap<int, FmlObjectHandle> evaluators;
    
    PiecewiseEvaluator( const std::string name, int region, FmlObjectHandle valueType, bool _isVirtual );
};


class AggregateEvaluator :
    public Evaluator
{
public:
    SimpleMap<FmlObjectHandle, FmlObjectHandle> binds;
    SimpleMap<int, FmlObjectHandle> evaluators;
    
    FmlObjectHandle indexEvaluator;
    
    AggregateEvaluator( const std::string _name, int _region, FmlObjectHandle _valueType, bool _isVirtual );
};


class AbstractEvaluator :
    public Evaluator
{
public:
    AbstractEvaluator( const std::string name, int region, FmlObjectHandle _valueType, bool _isVirtual );
};


class ExternalEvaluator :
    public Evaluator
{
public:
    ExternalEvaluator( const std::string name, int region, FmlObjectHandle _valueType, bool _isVirtual );
};


class DataLocation
{
public:
    const DataLocationType locationType;
    
    virtual ~DataLocation();

protected:
    DataLocation( DataLocationType _locationType );
};


class UnknownDataLocation :
    public DataLocation
{
public:
    UnknownDataLocation();
};


class InlineDataLocation :
     public DataLocation
{
public:
    const char *data;
    int length;
    
    InlineDataLocation();
    
    virtual ~InlineDataLocation();
};


class FileDataLocation :
    public DataLocation
{
public:
    std::string filename;
    int offset;
    DataFileType fileType;

    FileDataLocation();
};


class DataDescription
{
public:
    const DataDescriptionType descriptionType;
    
    virtual ~DataDescription();

protected:
    DataDescription( DataDescriptionType _descriptionType );
};


class UnknownDataDescription :
    public DataDescription
{
public:
    UnknownDataDescription();
};


class SemidenseDataDescription :
    public DataDescription
{
public:
    std::vector<FmlObjectHandle> sparseIndexes;
    std::vector<FmlObjectHandle> denseIndexes;
    std::vector<FmlObjectHandle> denseSets;
    
    const int *swizzle;
    int swizzleCount;

    DataLocation *dataLocation;
    
    SemidenseDataDescription();
    
    virtual ~SemidenseDataDescription();
};


class ParameterEvaluator :
    public Evaluator
{
public:
    DataDescription *dataDescription;
    
    ParameterEvaluator( const std::string _name, int _region, FmlObjectHandle _valueType, bool _isVirtual );
    
    virtual ~ParameterEvaluator();
};

const char *Fieldml_GetName( FmlHandle handle );

int Fieldml_SetRoot( FmlHandle, const char *root );


#endif //H_FIELDML_STRUCTS
