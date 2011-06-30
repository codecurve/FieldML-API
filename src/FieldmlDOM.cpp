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

#include <cstring>
#include <cstdio>
#include <vector>

#include <libxml/globals.h>
#include <libxml/xmlerror.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlschemas.h>

#include "Util.h"
#include "String_InternalLibrary.h"
#include "String_InternalXSD.h"
#include "string_const.h"

#include "FieldmlDOM.h"

using namespace std;

//========================================================================

struct ParseState
{
    FmlSessionHandle session;
    FieldmlErrorHandler *errorHandler;
    vector<xmlNodePtr> parseStack;
    vector<xmlNodePtr> unparsedNodes;
};

//========================================================================

class NodeParser
{
public:
    virtual int parseNode( xmlNodePtr node, ParseState &state ) = 0;
};

//========================================================================

static void addContextError( void *context, const char *msg, ... )
{
    FieldmlErrorHandler *errorHandler = (FieldmlErrorHandler*)context;

    char message[256];

    va_list vargs;  
    va_start( vargs, msg );  
    int retval = vsnprintf( message, 255, msg, vargs );  
    va_end( vargs);
    
    if( retval > 0 )
    {
        //libxml likes to put \n at the end of its error messages
        if( message[retval-1] == '\n' )
        {
            message[retval-1] = 0;
        }
        errorHandler->addError( message );
    }
}

//========================================================================

static int validate( FieldmlErrorHandler *errorHandler, xmlParserInputBufferPtr buffer, const char *resourceName )
{
    xmlSchemaPtr schemas = NULL;
    xmlSchemaParserCtxtPtr sctxt;
    xmlSchemaValidCtxtPtr vctxt;
    
    LIBXML_TEST_VERSION

    xmlSubstituteEntitiesDefault( 1 );

    if( buffer == NULL )
    {
        return 1;
    }

    sctxt = xmlSchemaNewMemParserCtxt( FML_STRING_FIELDML_XSD, strlen( FML_STRING_FIELDML_XSD ) );
    xmlSchemaSetParserErrors( sctxt, (xmlSchemaValidityErrorFunc)addContextError, (xmlSchemaValidityWarningFunc)addContextError, errorHandler );
    schemas = xmlSchemaParse( sctxt );
    if( schemas == NULL )
    {
        xmlGenericError( xmlGenericErrorContext, "Internal schema failed to compile\n" );
    }
    xmlSchemaFreeParserCtxt( sctxt );

    vctxt = xmlSchemaNewValidCtxt( schemas );
    xmlSchemaSetValidErrors( vctxt, (xmlSchemaValidityErrorFunc)addContextError, (xmlSchemaValidityWarningFunc)addContextError, errorHandler );

    int result = xmlSchemaValidateStream( vctxt, buffer, (xmlCharEncoding)0, NULL, NULL );

    xmlSchemaFreeValidCtxt( vctxt );

    xmlSchemaFree( schemas );
    
    return result;
}


static bool checkName( xmlNodePtr node, const xmlChar *name )
{
    return ( strcmp( ( char*)node->name, (char*)name ) == 0 );
}


const char *getStringAttribute( xmlNodePtr node, const xmlChar *attribute, const xmlChar *ns = NULL )
{
    if( ( node == NULL ) || ( attribute == NULL ) )
    {
        return NULL;
    }
    
    if( ns == NULL )
    {
        return (const char*)xmlGetNoNsProp( node, attribute );
    }
    else
    {
        return (const char*)xmlGetNsProp( node, attribute, ns );
    }
}


int getIntAttribute( xmlNodePtr node, const xmlChar *attribute, int defaultValue, const xmlChar *ns = NULL )
{
    const char *rawAttribute = getStringAttribute( node, attribute, ns );

    return ( rawAttribute != NULL ) ? atoi( rawAttribute ) : defaultValue;
}


static int parseObjectNode( xmlNodePtr objectNode, ParseState &state );

FmlObjectHandle getObjectAttribute( xmlNodePtr node, const xmlChar *attribute, ParseState &state )
{
    const char *objectName = getStringAttribute( node, attribute );
    if( objectName == NULL )
    {
        return FML_INVALID_HANDLE;
    }

    for( vector<xmlNodePtr>::iterator i = state.unparsedNodes.begin(); i != state.unparsedNodes.end(); i++ )
    {
        const char *nodeObjectName = getStringAttribute( *i, NAME_ATTRIB );
        if( strcmp( objectName, nodeObjectName ) == 0 )
        {
            parseObjectNode( *i, state );
            break;
        }
    }

    return Fieldml_GetObjectByName( state.session, objectName );
}


static xmlNodePtr getFirstChild( xmlNodePtr node, const xmlChar *name )
{
    int err = 0;
    
    xmlNodePtr cur = xmlFirstElementChild( node );
    while( cur != NULL )
    {
        if( checkName( cur, name ) )
        {
            return cur;
        }
        cur = xmlNextElementSibling( cur );
    }
    
    return NULL;
}


static int processChildren( xmlNodePtr node, const xmlChar *name, ParseState &state, NodeParser &parser )
{
    int err = 0;
    
    if( node == NULL )
    {
        return 0;
    }
    
    xmlNodePtr cur = xmlFirstElementChild( node );
    while( cur != NULL )
    {
        if( checkName( cur, name ) )
        {
            err = parser.parseNode( cur, state );
            
            if( err != 0 )
            {
                break;
            }
        }
        cur = xmlNextElementSibling( cur );
    }
    
    return err;
}


class ImportEntryParser :
    public NodeParser
{
private:
    const int index;
    
public:
    ImportEntryParser( int _index ) :
        index( _index ) {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *localName = getStringAttribute( node, LOCAL_NAME_ATTRIB );
        const char *remoteName = getStringAttribute( node, REMOTE_NAME_ATTRIB );
        
        FmlObjectHandle handle = Fieldml_AddImport( state.session, index, localName, remoteName );
        
        if( handle == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Invalid import specification" );
            return 1;
        }
        
        return 0;
    }
};


class ImportParser :
    public NodeParser
{
public:
    ImportParser() {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        int err = 0;
        const char *location = getStringAttribute( node, HREF_ATTRIB, XLINK_NAMESPACE_STRING );
        const char *region = getStringAttribute( node, REGION_ATTRIB );
    
        int index = Fieldml_AddImportSource( state.session, location, region );
        if( index < 0 )
        {
            state.errorHandler->logError( "Invalid import source specification", location );
            return 1;
        }
        
        ImportEntryParser importEntryParser( index );
        
        err = processChildren( node, IMPORT_TYPE_TAG, state, importEntryParser );
        if( err != 0 )
        {
            return err;
        }
        
        err = processChildren( node, IMPORT_EVALUATOR_TAG, state, importEntryParser );
        if( err != 0 )
        {
            return err;
        }
    
        return err;
    }
};


class TextDataSourceParser :
    public NodeParser
{
private:
    const FmlObjectHandle resource;

public:
    TextDataSourceParser( FmlObjectHandle _resource ) :
        resource( _resource ) {}

    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *name = getStringAttribute( node, NAME_ATTRIB );
        int firstLine = getIntAttribute( node, FIRST_LINE_ATTRIB, 1 );
        int count = getIntAttribute( node, COUNT_ATTRIB, -1 );
        int length = getIntAttribute( node, LENGTH_ATTRIB, -1 );
        int head = getIntAttribute( node, HEAD_ATTRIB, 0 );
        int tail = getIntAttribute( node, TAIL_ATTRIB, 0 );
        
        FmlObjectHandle dataSource = Fieldml_CreateTextDataSource( state.session, name, resource, firstLine, count, length, head, tail );
        if( dataSource == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Malformed TextDataSource entry data" );
            return 1;
        }
        
        return 0;
    }
};

    
class TextFileResourceParser :
    public NodeParser
{
public:
    TextFileResourceParser() {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *name = getStringAttribute( node, NAME_ATTRIB );
        const char *href = getStringAttribute( node, HREF_ATTRIB, XLINK_NAMESPACE_STRING );
    
        FmlObjectHandle resource = Fieldml_CreateTextFileDataResource( state.session, name, href );
        if( resource == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Invalid text file resource specification", name );
            return 1;
        }
        
        int err = processChildren( node, TEXT_DATA_SOURCE_TAG, state, TextDataSourceParser( resource ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};


class TextStringParser :
    public NodeParser
{
private:
    const FmlObjectHandle resource;
    
public:
    TextStringParser( FmlObjectHandle _resource ) :
        resource( _resource ) {}

    int parseNode( xmlNodePtr node, ParseState &state )
    {
        char *content = (char *)xmlNodeGetContent( node );
        
        FmlErrorNumber err = Fieldml_AddInlineData( state.session, resource, content, strlen( content ) );
        if( err != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Error adding text to text inline data resource" );
            return 1;
        }
        
        return 0;
    }
};


class TextInlineResourceParser :
    public NodeParser
{
public:
    TextInlineResourceParser() {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *name = getStringAttribute( node, NAME_ATTRIB );
    
        FmlObjectHandle resource = Fieldml_CreateTextInlineDataResource( state.session, name );
        if( resource == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Invalid text inline resource specification", name );
            return 1;
        }
        
        int err = processChildren( node, TEXT_STRING_TAG, state, TextStringParser( resource ) );
        if( err != 0 )
        {
            return err;
        }
    
        err = processChildren( node, TEXT_DATA_SOURCE_TAG, state, TextDataSourceParser( resource ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};


class ArrayDataSourceParser :
    public NodeParser
{
private:
    const FmlObjectHandle resource;

public:
    ArrayDataSourceParser( FmlObjectHandle _resource ) :
        resource( _resource ) {}

    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *name = getStringAttribute( node, NAME_ATTRIB );
        const char *sourceName = getStringAttribute( node, SOURCE_NAME_ATTRIB );
        
        FmlObjectHandle dataSource = Fieldml_CreateArrayDataSource( state.session, name, resource, sourceName );
        if( dataSource == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Malformed ArrayDataSource" );
            return 1;
        }
        
        return 0;
    }
};

    
class ArrayDataResourceParser :
    public NodeParser
{
public:
    ArrayDataResourceParser() {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        const char *name = getStringAttribute( node, NAME_ATTRIB );
        const char *href = getStringAttribute( node, HREF_ATTRIB, XLINK_NAMESPACE_STRING );
        const char *format = getStringAttribute( node, FORMAT_ATTRIB );
    
        FmlObjectHandle resource = Fieldml_CreateArrayDataResource( state.session, name, format, href );
        if( resource == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Invalid array data resource specification", name );
            return 1;
        }
        
        int err = processChildren( node, ARRAY_DATA_SOURCE_TAG, state, ArrayDataSourceParser( resource ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};


class BindParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;
    
public:
    BindParser( FmlObjectHandle _object ) :
        object( _object ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        FmlObjectHandle argument = getObjectAttribute( objectNode, ARGUMENT_ATTRIB, state );
        FmlObjectHandle source = getObjectAttribute( objectNode, SOURCE_ATTRIB, state );
        
        if( Fieldml_SetBind( state.session, object, argument, source ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Incompatible bind",
                getStringAttribute( objectNode, ARGUMENT_ATTRIB ),
                getStringAttribute( objectNode, SOURCE_ATTRIB ) );
            return 1;
        }
        
        return 0;
    }
};
    

class BindIndexParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;
    
public:
    BindIndexParser( FmlObjectHandle _object ) :
        object( _object ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        FmlObjectHandle argument = getObjectAttribute( objectNode, ARGUMENT_ATTRIB, state );
        int indexNumber = getIntAttribute( objectNode, INDEX_NUMBER_ATTRIB, -1 );
        
        if( Fieldml_SetIndexEvaluator( state.session, object, indexNumber, argument ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Incompatible index bind", getStringAttribute( objectNode, ARGUMENT_ATTRIB ) );
            return 1;
        }
        
        return 0;
    }
};
    

class ArgumentParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;

public:
    ArgumentParser( FmlObjectHandle _object ) :
        object( _object ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        FmlObjectHandle argument = getObjectAttribute( objectNode, NAME_ATTRIB, state );
    
        if( Fieldml_AddArgument( state.session, object, argument ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Bad argument", getStringAttribute( objectNode, NAME_ATTRIB ) );
            return 1;
        }
        
        return 0;
    }
};


class ReferenceEvaluatorParser :
    public NodeParser
{
public:
    ReferenceEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle sourceEvaluator = getObjectAttribute( objectNode, EVALUATOR_ATTRIB, state );
        
        FmlObjectHandle evaluator = Fieldml_CreateReferenceEvaluator( state.session, name, sourceEvaluator );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "ReferenceEvaluator creation failed", name );
            return 1;
        }
        
        int err = processChildren( getFirstChild( objectNode, BINDINGS_TAG ), BIND_TAG, state, BindParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};
    


class ContinuousTypeParser :
    public NodeParser
{
private:
    const FmlObjectHandle mesh;
        
public:
    ContinuousTypeParser( FmlObjectHandle _mesh ) :
        mesh( _mesh ) {}
    
    ContinuousTypeParser() :
        mesh( FML_INVALID_HANDLE ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
    
        FmlObjectHandle handle;
        if( mesh != FML_INVALID_HANDLE )
        {
            handle = Fieldml_CreateMeshChartType( state.session, mesh, name );
        }
        else
        {
            handle = Fieldml_CreateContinuousType( state.session, name );
        }
    
        if( handle == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "ContinuousType creation failed", name );
            return 1;
        }
    
        xmlNodePtr componentsNode = getFirstChild( objectNode, COMPONENTS_TAG );
        if( componentsNode != NULL )
        {
            const char *componentName = getStringAttribute( componentsNode, NAME_ATTRIB );
            int count = getIntAttribute( componentsNode, COUNT_ATTRIB, 0 );
            
            FmlObjectHandle components = Fieldml_CreateContinuousTypeComponents( state.session, handle, componentName, count );
            if( components == FML_INVALID_HANDLE )
            {
                state.errorHandler->logError( "ContinuousType has invalid component specification", name );
                return 1;
            }
        }
        
        return 0;
    }
};
    

class EnsembleTypeParser :
    public NodeParser
{
private:
    const FmlObjectHandle mesh;
    
public:
    EnsembleTypeParser( FmlObjectHandle _mesh ) :
        mesh( _mesh ) {}
    
    EnsembleTypeParser() :
        mesh( FML_INVALID_HANDLE ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        
        FmlObjectHandle handle;
        if( mesh != FML_INVALID_HANDLE )
        {
            handle = Fieldml_CreateMeshElementsType( state.session, mesh, name );
        }
        else
        {
            handle = Fieldml_CreateEnsembleType( state.session, name );
        }
        
        if( handle == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "EnsembleType creation failed", name );
            return 1;
        }
        
        xmlNodePtr membersNode = getFirstChild( objectNode, MEMBERS_TAG );
        if( membersNode == NULL )
        {
            state.errorHandler->logError( "EnsembleType must have members", name );
            return 1;
        }
        
        xmlNodePtr membersSpecNode = xmlFirstElementChild( membersNode );
        if( membersSpecNode == NULL )
        {
            state.errorHandler->logError( "EnsembleType must have members", name );
            return 1;
        }
    
        if( checkName( membersSpecNode, MEMBER_RANGE_TAG ) )
        {
            int min = getIntAttribute( membersSpecNode, MIN_ATTRIB, -1 );
            int max = getIntAttribute( membersSpecNode, MAX_ATTRIB, -1 );
            int stride = getIntAttribute( membersSpecNode, STRIDE_ATTRIB, 1 );
            
            if( Fieldml_SetEnsembleMembersRange( state.session, handle, min, max, stride ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "EnsembleType has invalid range specification", name );
                return 1;
            }
        }
        else if( ( checkName( membersSpecNode, MEMBER_LIST_DATA_TAG ) ) ||
            ( checkName( membersSpecNode, MEMBER_RANGE_DATA_TAG ) ) ||
            ( checkName( membersSpecNode, MEMBER_STRIDE_RANGE_DATA_TAG ) ) )
        {
            EnsembleMembersType type;
            
            if( checkName( membersSpecNode, MEMBER_LIST_DATA_TAG ) )
            {
                type = MEMBER_LIST_DATA;
            }
            else if( checkName( membersSpecNode, MEMBER_RANGE_DATA_TAG ) )
            {
                type = MEMBER_RANGE_DATA;
            }
            else if( checkName( membersSpecNode, MEMBER_STRIDE_RANGE_DATA_TAG ) )
            {
                type = MEMBER_STRIDE_RANGE_DATA;
            }
    
            FmlObjectHandle dataObject = getObjectAttribute( membersSpecNode, DATA_ATTRIB, state );
            int count = getIntAttribute( membersSpecNode, COUNT_ATTRIB, -1 );
            
            if( Fieldml_SetEnsembleMembersDataSource( state.session, handle, type, count, dataObject ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "EnsembleType has invalid data specification", name );
                return 1;
            }
        }
        else
        {
            state.errorHandler->logError( "EnsembleType has unknown member specification", (char*)membersSpecNode->name );
            return 1;
        }
        
        return 0;
    }
};


class MeshShapeParser :
    public NodeParser 
{
private:
    const FmlObjectHandle mesh;
    
public:
    MeshShapeParser( FmlObjectHandle _mesh ) :
        mesh( _mesh ) {}
    
    int parseNode( xmlNodePtr shapeNode, ParseState &state )
    {
        const int element = getIntAttribute( shapeNode, KEY_ATTRIB, -1 );
        const char *shape = getStringAttribute( shapeNode, VALUE_ATTRIB );
        
        if( Fieldml_SetMeshElementShape( state.session, mesh, element, shape ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Invalid element/shape combination" );
            return 1;
        }
        
        return 0;
    }
};

    
class MeshTypeParser :
    NodeParser
{
public:
    MeshTypeParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        
        FmlObjectHandle handle = Fieldml_CreateMeshType( state.session, name );
        if( handle == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "MeshType creation failed", name );
            return 1;
        }
        
        xmlNodePtr elementsNode = getFirstChild( objectNode, ELEMENTS_TAG );
        if( elementsNode == NULL )
        {
            state.errorHandler->logError( "MeshType must have elements", name );
            return 1;
        }
        
        EnsembleTypeParser elementsParser( handle );
        int err = elementsParser.parseNode( elementsNode, state );
        if( err != 0 )
        {
            return err;
        }
        
        xmlNodePtr chartNode = getFirstChild( objectNode, CHART_TAG );
        if( chartNode == NULL )
        {
            state.errorHandler->logError( "MeshType must have chart specification", name );
            return 1;
        }
        
        ContinuousTypeParser chartParser( handle );
        err = chartParser.parseNode( chartNode, state );
        if( err != 0 )
        {
            return err;
        }
        
        xmlNodePtr shapesNode = getFirstChild( objectNode, MESH_SHAPES_TAG );
        if( shapesNode == NULL )
        {
            state.errorHandler->logError( "MeshType must have shape specification", name );
            return 1;
        }
    
        const char *defaultValue = getStringAttribute( shapesNode, DEFAULT_ATTRIB );
        if( defaultValue != NULL )
        {
            Fieldml_SetMeshDefaultShape( state.session, handle, defaultValue );
        }

        return processChildren( shapesNode, MESH_SHAPE_TAG, state, MeshShapeParser( handle ) );
    }
};
    

class ArgumentEvaluatorParser :
    public NodeParser
{
public:
    ArgumentEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle valueType = getObjectAttribute( objectNode, VALUE_TYPE_ATTRIB, state );
        
        FmlObjectHandle evaluator = Fieldml_CreateArgumentEvaluator( state.session, name, valueType );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "Cannot create ArgumentEvaluator with given type", name);
            return 1;
        }
        
        return processChildren( getFirstChild( objectNode, ARGUMENTS_TAG ), ARGUMENT_TAG, state, ArgumentParser( evaluator ) );
    }
};
    

class ExternalEvaluatorParser :
    public NodeParser
{
public:
    ExternalEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle valueType = getObjectAttribute( objectNode, VALUE_TYPE_ATTRIB, state );
        
        FmlObjectHandle evaluator = Fieldml_CreateExternalEvaluator( state.session, name, valueType );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "ExternalEvaluator creation failed", name );
            return 1;
        }
        
        return processChildren( getFirstChild( objectNode, ARGUMENTS_TAG ), ARGUMENT_TAG, state, ArgumentParser( evaluator ) );
    }
};


class PiecewiseMapParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;
    
public:
    PiecewiseMapParser( FmlObjectHandle _object ) :
        object( _object ) {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        int element = getIntAttribute( node, INDEX_VALUE_ATTRIB, -1 );
        FmlObjectHandle evaluator = getObjectAttribute( node, EVALUATOR_ATTRIB, state );
        
        if( Fieldml_SetEvaluator( state.session, object, element, evaluator ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "PiecewiseEvaluator creation failed" );
            return 1;
        }
    
        return 0;
    }
};
    

class IndexEvaluatorParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;
    
public:
    IndexEvaluatorParser( FmlObjectHandle _object ) :
        object( _object ) {}
    
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        FmlObjectHandle indexHandle = getObjectAttribute( node, EVALUATOR_ATTRIB, state );
        int index = getIntAttribute( node, INDEX_NUMBER_ATTRIB, -1 );
    
        if( Fieldml_SetIndexEvaluator( state.session, object, index, indexHandle ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Invalid index evaluation" );
            return 1;
        }
        
        return 0;
    }
};
    

class PiecewiseEvaluatorParser :
    public NodeParser
{
public:
    PiecewiseEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle valueType = getObjectAttribute( objectNode, VALUE_TYPE_ATTRIB, state );
    
        FmlObjectHandle evaluator = Fieldml_CreatePiecewiseEvaluator( state.session, name, valueType );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "PiecewiseEvaluator creation failed", name );
            return 1;
        }
        
        xmlNodePtr evaluatorsNode = getFirstChild( objectNode, ELEMENT_EVALUATORS_TAG );
        if( evaluatorsNode == NULL )
        {
            state.errorHandler->logError( "PiecewiseEvaluator must have element evaluators", name );
            return 1;
        }
    
        FmlObjectHandle defaultHandle = getObjectAttribute( evaluatorsNode, DEFAULT_ATTRIB, state );
        if( defaultHandle != FML_INVALID_HANDLE )
        {
            if( Fieldml_SetDefaultEvaluator( state.session, evaluator, defaultHandle ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "PiecewiseEvaluator has an invalid default", name );
                return 1;
            }
        }
        
        int err = processChildren( evaluatorsNode, ELEMENT_EVALUATOR_TAG, state, PiecewiseMapParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
    
        err = processChildren( getFirstChild( objectNode, BINDINGS_TAG ), BIND_TAG, state, BindParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
    
        err = processChildren( getFirstChild( objectNode, INDEX_EVALUATORS_TAG ), INDEX_EVALUATOR_TAG, state, IndexEvaluatorParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};


class AggregateMapParser :
    public NodeParser
{
private:
    const FmlObjectHandle object;
    
public:
    AggregateMapParser( FmlObjectHandle _object ) :
        object( _object ) {}
        
    int parseNode( xmlNodePtr node, ParseState &state )
    {
        int element = getIntAttribute( node, COMPONENT_ATTRIB, -1 );
        FmlObjectHandle evaluator = getObjectAttribute( node, EVALUATOR_ATTRIB, state );
        
        if( Fieldml_SetEvaluator( state.session, object, element, evaluator ) != FML_ERR_NO_ERROR )
        {
            state.errorHandler->logError( "Invalid component evaluation" );
            return 1;
        }
    
        return 0;
    }
};


class AggregateEvaluatorParser :
    public NodeParser
{
public:
    AggregateEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle valueType = getObjectAttribute( objectNode, VALUE_TYPE_ATTRIB, state );
    
        FmlObjectHandle evaluator = Fieldml_CreateAggregateEvaluator( state.session, name, valueType );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "AggregateEvaluator creation failed", name );
            return 1;
        }
        
        xmlNodePtr evaluatorsNode = getFirstChild( objectNode, COMPONENT_EVALUATORS_TAG );
        if( evaluatorsNode == NULL )
        {
            state.errorHandler->logError( "AggregateEvaluator must have component evaluators", name );
            return 1;
        }
    
        FmlObjectHandle defaultHandle = getObjectAttribute( evaluatorsNode, DEFAULT_ATTRIB, state );
        if( defaultHandle != FML_INVALID_HANDLE )
        {
            if( Fieldml_SetDefaultEvaluator( state.session, evaluator, defaultHandle ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "AggregateEvaluator has an invalid default", name );
                return 1;
            }
        }
        
        int err = processChildren( evaluatorsNode, COMPONENT_EVALUATOR_TAG, state, AggregateMapParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
    
        err = processChildren( getFirstChild( objectNode, BINDINGS_TAG ), BIND_TAG, state, BindParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
        
        err = processChildren( getFirstChild( objectNode, BINDINGS_TAG ), BIND_INDEX_TAG, state, BindIndexParser( evaluator ) );
        if( err != 0 )
        {
            return err;
        }
        
        return 0;
    }
};

    
class SemidenseIndexEvaluatorParser :
    public NodeParser
{
private:
    const FmlObjectHandle evaluator;
    const bool isDense;
    
public:
    SemidenseIndexEvaluatorParser( FmlObjectHandle _evaluator, bool _isDense ) :
        evaluator( _evaluator ), isDense( _isDense ) {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        FmlObjectHandle handle = getObjectAttribute( objectNode, EVALUATOR_ATTRIB, state );
        FmlObjectHandle orderHandle = getObjectAttribute( objectNode, ORDER_ATTRIB, state );

        if( isDense )
        {
            if( Fieldml_AddDenseIndexEvaluator( state.session, evaluator, handle, orderHandle ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "Invalid dense index evaluator in semi dense data", evaluator );
                return 1;
            }
        }
        else
        {
            if( Fieldml_AddSparseIndexEvaluator( state.session, evaluator, handle ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "Invalid sparse index evaluator in semi dense data", evaluator );
                return 1;
            }
        }
        
        return 0;
    }
};
    

class ParameterEvaluatorParser :
    public NodeParser
{
public:
    ParameterEvaluatorParser() {}
    
    int parseNode( xmlNodePtr objectNode, ParseState &state )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        FmlObjectHandle valueType = getObjectAttribute( objectNode, VALUE_TYPE_ATTRIB, state );

        FmlObjectHandle evaluator = Fieldml_CreateParameterEvaluator( state.session, name, valueType );
        if( evaluator == FML_INVALID_HANDLE )
        {
            state.errorHandler->logError( "ParameterEvaluator creation failed", name );
            return 1;
        }

        xmlNodePtr semidenseNode = getFirstChild( objectNode, SEMI_DENSE_DATA_TAG );
        xmlNodePtr denseNode = getFirstChild( objectNode, DENSE_ARRAY_DATA_TAG );
        xmlNodePtr dokNode = getFirstChild( objectNode, DOK_ARRAY_DATA_TAG );
        if( semidenseNode != NULL )
        {
            if( Fieldml_SetParameterDataDescription( state.session, evaluator, DESCRIPTION_SEMIDENSE ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid data description", name );
                return 1;
            }
            
            FmlObjectHandle dataObject = getObjectAttribute( semidenseNode, DATA_ATTRIB, state );

            if( Fieldml_SetDataSource( state.session, evaluator, dataObject ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid data source", name );
                return 1;
            }

            int err = processChildren( getFirstChild( semidenseNode, DENSE_INDEXES_TAG ), INDEX_EVALUATOR_TAG, state, SemidenseIndexEvaluatorParser( evaluator, true ) );
            if( err != 0 )
            {
                return err;
            }
            
            err = processChildren( getFirstChild( semidenseNode, SPARSE_INDEXES_TAG ), INDEX_EVALUATOR_TAG, state, SemidenseIndexEvaluatorParser( evaluator, false ) );
            if( err != 0 )
            {
                return err;
            }
        }
        else if( denseNode != NULL )
        {
            if( Fieldml_SetParameterDataDescription( state.session, evaluator, DESCRIPTION_DENSE_ARRAY ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid data description", name );
                return 1;
            }
            
            FmlObjectHandle dataObject = getObjectAttribute( denseNode, DATA_ATTRIB, state );
            
            if( Fieldml_SetDataSource( state.session, evaluator, dataObject ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid data source", name );
                return 1;
            }

            int err = processChildren( getFirstChild( denseNode, DENSE_INDEXES_TAG ), INDEX_EVALUATOR_TAG, state, SemidenseIndexEvaluatorParser( evaluator, true ) );
            if( err != 0 )
            {
                return err;
            }
        }
        else if( dokNode != NULL )
        {
            if( Fieldml_SetParameterDataDescription( state.session, evaluator, DESCRIPTION_DOK_ARRAY ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid data description", name );
                return 1;
            }
            
            FmlObjectHandle keyDataObject = getObjectAttribute( dokNode, KEY_DATA_ATTRIB, state );
            FmlObjectHandle valueDataObject = getObjectAttribute( dokNode, VALUE_DATA_ATTRIB, state );
            
            if( Fieldml_SetKeyDataSource( state.session, evaluator, keyDataObject ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid key data source", name );
                return 1;
            }
            if( Fieldml_SetDataSource( state.session, evaluator, valueDataObject ) != FML_ERR_NO_ERROR )
            {
                state.errorHandler->logError( "ParameterEvaluator must have a valid value data source", name );
                return 1;
            }

            int err = processChildren( getFirstChild( dokNode, SPARSE_INDEXES_TAG ), INDEX_EVALUATOR_TAG, state, SemidenseIndexEvaluatorParser( evaluator, false ) );
            if( err != 0 )
            {
                return err;
            }
            
            err = processChildren( getFirstChild( dokNode, DENSE_INDEXES_TAG ), INDEX_EVALUATOR_TAG, state, SemidenseIndexEvaluatorParser( evaluator, true ) );
            if( err != 0 )
            {
                return err;
            }
        }
        else
        {
            state.errorHandler->logError( "ParameterEvaluator must have a description", name );
            return 1;
        }

        return 0;
    }
};
    
    
static int parseObjectNode( xmlNodePtr objectNode, ParseState &state )
{
    if( FmlUtil::contains( state.parseStack, objectNode ) )
    {
        const char *name = getStringAttribute( objectNode, NAME_ATTRIB );
        state.errorHandler->logError( "Recursive object definition", name );
        return 1;
    }
    
    state.parseStack.push_back( objectNode );
    
    int err = 0;
    if( checkName( objectNode, TEXT_FILE_RESOURCE_TAG ) )
    {
        err = TextFileResourceParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, TEXT_INLINE_RESOURCE_TAG ) )
    {
        err = TextInlineResourceParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, ARRAY_DATA_RESOURCE_TAG ) )
    {
        err = ArrayDataResourceParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, REFERENCE_EVALUATOR_TAG ) )
    {
        err = ReferenceEvaluatorParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, CONTINUOUS_TYPE_TAG ) )
    {
        err = ContinuousTypeParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, ENSEMBLE_TYPE_TAG ) )
    {
        err = EnsembleTypeParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, MESH_TYPE_TAG ) )
    {
        err = MeshTypeParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, ARGUMENT_EVALUATOR_TAG ) )
    {
        err = ArgumentEvaluatorParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, EXTERNAL_EVALUATOR_TAG ) )
    {
        err = ExternalEvaluatorParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, PIECEWISE_EVALUATOR_TAG ) )
    {
        err = PiecewiseEvaluatorParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, AGGREGATE_EVALUATOR_TAG ) )
    {
        err = AggregateEvaluatorParser().parseNode( objectNode, state );
    }
    else if( checkName( objectNode, PARAMETER_EVALUATOR_TAG ) )
    {
        err = ParameterEvaluatorParser().parseNode( objectNode, state );
    }
    
    state.parseStack.pop_back();
    
    vector<xmlNodePtr>::iterator loc = find( state.unparsedNodes.begin(), state.unparsedNodes.end(), objectNode );

    state.unparsedNodes.erase( loc );

    return err;
}


static int parseDoc( xmlDocPtr doc, ParseState &state )
{
    int err = 0;
    
    xmlNodePtr fieldmlNode = xmlDocGetRootElement( doc );
    
    xmlNodePtr regionNode = xmlFirstElementChild( fieldmlNode );
    
    if( !checkName( regionNode, REGION_TAG ) )
    {
        return 1;
    }

    ImportParser importParser;
    xmlNodePtr cur = xmlFirstElementChild( regionNode );
    while( cur != NULL )
    {
        if( checkName( cur, IMPORT_TAG ) )
        {
            importParser.parseNode( cur, state );
        }
        else
        {
            state.unparsedNodes.insert( state.unparsedNodes.begin(), cur );
        }
        cur = xmlNextElementSibling( cur );
    }

    while( state.unparsedNodes.size() != 0 )
    {
        parseObjectNode( state.unparsedNodes.back(), state );
    }
    
    return 0;
}


int FieldmlDOM::parseFieldmlFile( const char *filename, FieldmlErrorHandler *errorHandler, FmlSessionHandle session )
{
    LIBXML_TEST_VERSION

    xmlSubstituteEntitiesDefault( 1 );

    xmlParserInputBufferPtr buffer = xmlParserInputBufferCreateFilename( filename, XML_CHAR_ENCODING_NONE );
    if( buffer == NULL )
    {
        errorHandler->logError( "Failed to create XML buffer", filename );
        return 1;
    }
    
    int err = validate( errorHandler, buffer, filename );
    if( err != 0 )
    {
        return err;
    }

    xmlParserCtxtPtr ctxt; /* the parser context */
    xmlDocPtr doc; /* the resulting document tree */

    /* create a parser context */
    ctxt = xmlNewParserCtxt();
    if( ctxt == NULL )
    {
        errorHandler->logError( "Failed to allocate XML parser context" );
        return 1;
    }
    /* parse the file, activating the DTD validation option */
    doc = xmlCtxtReadFile( ctxt, filename, NULL, 0 );
    /* check if parsing suceeded */
    if (doc == NULL)
    {
        errorHandler->logError( "Failed to parse XML file", filename );
    }
    else
    {
        ParseState state;
        
        state.errorHandler = errorHandler;
        state.session = session;
        parseDoc( doc, state );
        xmlFreeDoc( doc );
    }
    /* free up the parser context */
    xmlFreeParserCtxt( ctxt );
    
    return 0;
}


int FieldmlDOM::parseFieldmlString( const char *string, const char *stringDescription, const char *url, FieldmlErrorHandler *errorHandler, FmlSessionHandle session )
{
    LIBXML_TEST_VERSION

    xmlSubstituteEntitiesDefault( 1 );

    xmlParserInputBufferPtr buffer = xmlParserInputBufferCreateMem( string, strlen( string ), XML_CHAR_ENCODING_NONE );
    if( buffer == NULL )
    {
        errorHandler->logError( "Failed to create XML buffer", stringDescription );
        return 1;
    }
    
    int err = validate( errorHandler, buffer, stringDescription );
    if( err != 0 )
    {
        return err;
    }

    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    if( ctxt == NULL )
    {
        errorHandler->logError( "Failed to allocate parser context", stringDescription );
        return 1;
    }

    xmlDocPtr doc = xmlCtxtReadMemory( ctxt, string, strlen( string ), url, NULL, 0 );
    if( doc == NULL )
    {
        errorHandler->logError( "Failed to parse XML", stringDescription );
    }
    else
    {
        ParseState state;
        
        state.errorHandler = errorHandler;
        state.session = session;
        parseDoc( doc, state );
        xmlFreeDoc( doc );
    }

    xmlFreeParserCtxt( ctxt );
    
    return 0;
}