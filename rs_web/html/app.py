from __future__ import print_function # In python 2.7

from flask import Flask, render_template,current_app,request
from flask_paginate import Pagination, get_page_args

import sys

import re


from source import RSMongoClient as RSMC

app = Flask(__name__)
app.config.from_pyfile('app.cfg')

mc = RSMC.RSMongoClient('Scenes_annotated')


#@app.route('/rs_test.html')
#def hello(dbname=None,rows=[],images =[]):
#  imgs = getImages(1,10)
#  return render_template('objects.html', dbname=dbName,rows=timestamps,images=imgs)
mc.getObjectInstances(0)


@app.route('/', methods= ['GET','POST'])
@app.route('/scenes',methods= ['GET','POST'])
def index():
    print (request.form ,file = sys.stderr)
    if request.method == 'POST':
        query = request.form['console']
        param = re.search(r"\(([0-9])\)",query)
        if param:
            return findObjectInstances(int(param.group(1)))
        elif query == 'objects':
            return handle_objects()
    
    timestamps = mc.getTimestamps()
    total = len(timestamps)
    page, per_page, offset = get_page_args()
    
    idxB = (page-1)*per_page
    idxE = page*per_page  
    scenes = []
    for ts in timestamps[idxB:idxE]:
        scene = {}
        scene['ts']= ts
        scene['rgb'] = mc.getSceneImage(ts)
        scene['objects'] = mc.getObjectHypsForScene(ts)
        scenes.append(scene)
        
    pagination = get_pagination(page=page,
                                per_page=per_page,
                                total=total,
                                record_name='scenes',
                                format_total=True,
                                format_number=True,
                                )
    return render_template('objStore.html', 
                           scenes=scenes,
                           page=page,
                           per_page=per_page,
                           pagination=pagination,
                           )    

def handle_objects():
    objs = mc.getPersistentObjects()
    return render_template('objects.html', objects=objs)
  
def findObjectInstances(objID):
    objs = mc.getObjectInstances(objID)
    return render_template('objects.html', objects=objs)
    



def get_pagination(**kwargs):
    kwargs.setdefault('record_name', 'records')
    return Pagination(css_framework=get_css_framework(),
                      link_size=get_link_size(),
                      show_single_page=show_single_page_or_not(),
                      **kwargs
                      )
                      
def get_css_framework():
    return current_app.config.get('CSS_FRAMEWORK', 'bootstrap3')


def get_link_size():
    return current_app.config.get('LINK_SIZE', 'sm')


def show_single_page_or_not():
    return current_app.config.get('SHOW_SINGLE_PAGE', False)

if __name__ == '__main__':
    app.run(use_reloader=True, debug=True, host="0.0.0.0", threaded=True)
