import _ from 'underscore'
import React from 'react'

const ConfigInput = (props) => {
 
    const box_type = _.isNumber(props.value) ? "number" : "text"
    return (
        <div className='flex-row input-container'>
            <label  className='input-label light'>{props.param}</label>
            <input 
                value={props.value} type={box_type}  
                min="0" max="100" onChange={props.onChange} 
                className='textbox-input dark'
            />
        
        </div>
    )
    
}

export default ConfigInput
