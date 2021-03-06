#
# All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
# its licensors.
#
# For complete copyright and license terms please see the LICENSE at the root of this
# distribution (the "License"). All use of this software is governed by the License,
# or, if provided, by the license below or the license accompanying this file. Do not
# remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#
# $Revision: #1 $

import os.path

def get_default_staging_table_name():
    return 'StagingSettingsTable'

def get_default_bucket_name():
    return 'ContentBucket'

def get_default_resource_group():
    return 'CloudGemDynamicContent'
    
def get_default_request_lambda_name():
    return 'ContentRequest'
    
def get_max_days():
    return 365 * 100

def get_manifest_extension():
    return '.json'

def get_default_manifest_name():
    return 'default' + get_manifest_extension()

def get_manifest_pak_extension():
    return '.manifest.pak'

def get_pak_folder():
    return 'DynamicContent/Paks/'
    
def get_manifest_folder():
    return 'DynamicContent/Manifests/'

def get_public_certificate_folder():
    return 'DynamicContent/Certificates/'
  
def get_private_certificate_folder():
    return 'DynamicContent/Certificates/Private/'
    
def get_public_certificate_game_folder(game_folder_name):
    return os.path.join(game_folder_name,get_public_certificate_folder()).replace('\\','/')
 
def get_private_certificate_game_folder(game_folder_name):
    return os.path.join(game_folder_name,get_private_certificate_folder(),).replace('\\','/')
    
def get_manifest_game_folder(game_folder_name):
    return os.path.join(game_folder_name,get_manifest_folder()).replace('\\','/')
    
def get_pak_game_folder(game_folder_name):
    return os.path.join(game_folder_name,get_pak_folder()).replace('\\','/')
    
def get_keysize():
    return 2048
 
def get_default_keyname():
    return 'DynamicContent'
 
def get_public_keyname(base_name, extension):
    return '{}.pub.{}'.format(base_name or get_default_keyname(), extension)

def get_private_keyname(base_name,extension):
    return '{}.priv.{}'.format(base_name or get_default_keyname(), extension)